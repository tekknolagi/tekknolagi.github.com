---
title: "A baseline scrapscript compiler"
layout: post
date: 2024-06-01
---

[Scrapscript](https://scrapscript.org/) is a small, pure, functional,
content-addressable, network-first programming language.
```
fact 5
. fact =
  | 0 -> 1
  | n -> n * fact (n - 1)
```

My [previous post](/blog/scrapscript/) introduced the language a bit and then
talked about the interpreter that Chris and I built. This post is about the
compiler that Chris and I built.

## In the beginning, there was an interpreter

Writing a simple tree-walking interpreter is a great way to prototype a
language implementation. It requires the minimal amount of moving parts and can
be very compact. Our whole lexer, parser, and interpreter are altogether about
1300 lines of home-grown, dependency-free Python.

But then I took Olin Shivers' graduate course on compiling functional
programming languages and learned about continuation-passing style (CPS),
common functional optimizations, k-CFA, webs, and more. It got me thinking:
well, since I continue to describe Scrapscript as a "juiced-up lambda calculus",
maybe I should write a compiler for it.

I didn't have time this past term to do it for the course project and I did not
want to start by writing an optimizing compiler, so I decided to write a
baseline compiler in a similar vein to the baseline interpreter: the minimal
amount of code necessary to get something off the ground.

```console?prompt=$
$ wc -l runtime.c compiler.py
  721 runtime.c
  514 compiler.py
 1235 total
$
```

Turns out, the compiler core is roughly the same order of magnitude as the
interpreter core (sans parsing), and I suppose that makes sense since the
tree-walking code structure looks very similar. We also bundle a small runtime
to provide garbage collection and some other functions.

Let's take a look inside.

## Inside the compiler: expressions

The compiler analogue to the interpreter's `eval_exp` function is
`Compiler.compile`. It takes an environment and an expression and returns a
string representing the C variable name that corresponds to the result of
evaluating that expression.

```python
class Compiler:
    def compile(self, env: Env, exp: Object) -> str:
        if isinstance(exp, Int):
            return self._mktemp(f"mknum(heap, {exp.value})")
        if isinstance(exp, Binop):
            left = self.compile(env, exp.left)
            right = self.compile(env, exp.right)
            if exp.op == BinopKind.ADD:
                # ...
                return self._mktemp(f"num_add({left}, {right})")
        # ...

    def gensym(self, stem: str = "tmp") -> str:
        self.gensym_counter += 1
        return f"{stem}_{self.gensym_counter-1}"

    def _mktemp(self, exp: str) -> str:
        temp = self.gensym()
        return self._handle(temp, exp)
```

The compiler generates a temporary variable to hold intermediate results. This
turns code like `1 + 2` into something like:

```c
struct object *tmp_0 = mknum(heap, 1);
struct object *tmp_1 = mknum(heap, 2);
struct object *tmp_2 = num_add(tmp_0, tmp_1);
```

and then the compiler will return the name of the last temporary variable,
`tmp_2`. This is a little verbose, but it's the simplest way to linearize
Scrapscript's expression tree structure into C statements.

This is perhaps the simplest part of the compiler because it looks like every
other course project tree-walking procedural language compiler you might see.
Handling the functional nature of Scrapscript is where things get more
interesting.

## Inside the compiler: functions

Functional languages encourage creating and using functions all over the place.
This means that as we traverse the expression tree we also need to keep track
of all of the top-level C functions that we need to create to store their
code.

```python
class Compiler:
    def compile_function(self, env: Env, exp: Function, name: Optional[str]) -> str:
        assert isinstance(exp.arg, Var)
        # Make a top-level C function
        fn = self.make_compiled_function(exp.arg.name, exp, name)
        self.functions.append(fn)
        # Push a new compilation context for the function
        cur = self.function
        self.function = fn
        funcenv = self.compile_function_env(fn, name)
        # Compile the function's body expression into it
        val = self.compile(funcenv, exp.body)
        fn.code.append(f"return {val};")
        # Pop back to the previous compilation context
        self.function = cur
        return self.make_closure(env, fn)
```

Functions are also *values* in Scrapscript, so they can be passed around just
as any other data would be. In our compiler we call them closure objects
because they are a function paired with an environment. This is because
functions can also *bind data*.

For example, consider the following Scrapscript code that defines an anonymous
function:

```
x -> y -> x + y
```

Since Scrapscript functions take one parameter each, "multi-parameter"
functions are possible only by nesting functions. Here, the inner function only
takes a `y` parameter but still has access to the `x` value that was bound when
the outer function was called.

In order to determine what variables need to be stored in the closure---what
variables are *free* in a function---we can re-use the existing `free_in`
function from the interpreter. We use it in the interpreter to avoid making
closure objects so big that they contain the entire environment, and it turns
out to be really handy here too.

```python
class Compiler:
    def make_closure(self, env: Env, fn: CompiledFunction) -> str:
        name = self._mktemp(f"mkclosure(heap, {fn.name}, {len(fn.fields)})")
        for i, field in enumerate(fn.fields):
            self._emit(f"closure_set({name}, {i}, {env[field]});")
        return name
```

The `make_closure` function allocates a closure object (a `struct closure`) and
then sets the fields of the closure to the values of the free variables. They
are laid out linearly in memory and the compiler stores a mapping of variable
name to its index.

```c
struct closure {
  struct gc_obj HEAD;
  ClosureFn fn;
  size_t size;
  struct object* env[];
};
```

This is not very optimized. It would be great if we could avoid allocating a
closure object for every function, but that would require a more sophisticated
analysis: we would need to determine that a function has no free variables
(easy; done) and that it is not passed around as a value (not bad but requires
more analysis than we do right now).

The bulk of the work in the compiler was spent on the other kind of function
that we support: match functions.

## Inside the compiler: pattern matching

Scrapscript supports pattern matching similar to OCaml's `match` syntax:

```
| 0 -> 1
| [1, two, 3] -> two + 1
| [x, ...xs] -> x + sum xs
| { x = 1, y = z } -> z
| #tagged "value" -> 123
```

The above is a function that takes in some unnamed argument and immediately
tries to match it against the given patterns, top to bottom. If none of the
patterns match, the entire Scrapscript program aborts with an exception. (To
avoid this, add a useless `default` or `_` pattern at the end.)

Like OCaml and unlike Erlang[^variable-binding], variables bind names in the
patterns, so `two` is bound to the middle element of the list. It also supports
destructuring lists and records with the `...` syntax.

[^variable-binding]: What I mean about Erlang is that I sometimes miss the
    feature where you can use a variable in a pattern to check that it matches
    the existing value. For example, if `ThreadId` contains the name of the
    current thread, you can match against messages that are only for the
    current thread by writing `{ThreadId, msg_contents}`.

Implementing pattern matching took me a while. Matching integers and variables
was pretty easy but I got stuck on lists and records. Finally, I asked Chris if
he had time to pair on it and luckily he said yes. After two hours, we figured
it out.

It turns out that the key is writing the match function compiler *exactly* like
the interpreted version. We had the logic right the first time. Fancy stuff
like guaranteeing the minimal number of type checks can come later. To see what
I mean, take a look at snippets of the interpreted match and compiled match
side by side:

```python
# Interpreted
def match(obj: Object, pattern: Object) -> Optional[Env]:
    if isinstance(pattern, Int):
        return {} if isinstance(obj, Int) and obj.value == pattern.value else None
    if isinstance(pattern, Var):
        return {pattern.name: obj}
    if isinstance(pattern, List):
        if not isinstance(obj, List):
            return None
        result: Env = {}
        use_spread = False
        for i, pattern_item in enumerate(pattern.items):
            if isinstance(pattern_item, Spread):
                use_spread = True
                if pattern_item.name is not None:
                    result.update({pattern_item.name: List(obj.items[i:])})
                break
            if i >= len(obj.items):
                return None
            obj_item = obj.items[i]
            part = match(obj_item, pattern_item)
            if part is None:
                return None
            result.update(part)
        if not use_spread and len(pattern.items) != len(obj.items):
            return None
        return result
```

Gross, right? There are a bunch of edge cases for matching lists. It's a little
cleaner if you ignore the spread feature, but the basic structure looks like:

* Check that the input is a list
* For each item, check that there is a corresponding item in the input list and
  recursively match
* Update the environment with the bindings from the variable patterns

If there's a no match, return `None`. If there's a match but no bindings,
return an empty environment `{}`. If there's a match with bindings, propagate
the bindings upward.

The end state of the compiler version looks so similar:

```python
{% raw %}
class Compiler:
    def try_match(self, env: Env, arg: str, pattern: Object, fallthrough: str) -> Env:
        if isinstance(pattern, Int):
            self._emit(f"if (!is_num({arg})) {{ goto {fallthrough}; }}")
            self._emit(f"if (num_value({arg}) != {pattern.value}) {{ goto {fallthrough}; }}")
            return {}
        if isinstance(pattern, Var):
            return {pattern.name: arg}
        if isinstance(pattern, List):
            self._emit(f"if (!is_list({arg})) {{ goto {fallthrough}; }}")
            updates = {}
            the_list = arg
            use_spread = False
            for i, pattern_item in enumerate(pattern.items):
                if isinstance(pattern_item, Spread):
                    use_spread = True
                    if pattern_item.name:
                        updates[pattern_item.name] = the_list
                    break
                # Not enough elements
                self._emit(f"if (is_empty_list({the_list})) {{ goto {fallthrough}; }}")
                list_item = self._mktemp(f"list_first({the_list})")
                updates.update(self.try_match(env, list_item, pattern_item, fallthrough))
                the_list = self._mktemp(f"list_rest({the_list})")
            if not use_spread:
                # Too many elements
                self._emit(f"if (!is_empty_list({the_list})) {{ goto {fallthrough}; }}")
            return updates
{% endraw %}
```

I felt very silly after this. It's a nice showcase, though, of the advice that
people like to give about writing compilers: "just emit code that does what you
*would do* if you were in the interpreter".

## Inside the runtime: garbage collection

I showed a little snippet of runtime code earlier. This `struct closure` is an
example of a heap-allocated object in Scrapscript. Unlike in the interpreter
where we rely on the host Python runtime to garbage collect objects, in the
compiler we have to do it all by ourselves.

I adapted the initial semispace collector from Andy Wingo's [excellent blog
post](https://wingolog.org/archives/2022/12/10/a-simple-semi-space-collector).
The Wingo GC core provides a struct, an allocator function, and machinery for
sweeping the heap:

```c
struct gc_obj {
  uintptr_t tag;  // low bit is 0 if forwarding ptr
  uintptr_t payload[0];
};

struct gc_heap* make_heap(size_t size) {
void destroy_heap(struct gc_heap* heap);

struct gc_obj* allocate(struct gc_heap* heap, size_t size);
void collect(struct gc_heap* heap);
```

and relies on the user of the "library" to provide three functions that are
application-specific:

```c
// How big is the given object in memory? Probably does dispatch on the `tag`
// field in `gc_obj`.
size_t heap_object_size(struct gc_obj* obj);
// For every object pointer in the given object, call the given callback. For
// lists, for example, visit every list item.
size_t trace_heap_object(struct gc_obj* obj, struct gc_heap* heap,
                         VisitFn visit);
// For every root pointer in the application, call the given callback. For our
// use case, this is the shadow stack/handles (more on this later).
void trace_roots(struct gc_heap* heap, VisitFn visit);
```

## Inside the runtime: handles

## Cosmopolitan and WebAssembly

## Future projects

## Thanks for reading

Well first, play with [the web REPL](https://scrapscript.fly.dev/repl). Then
take a look at [the repo](https://github.com/tekknolagi/scrapscript) and start
contributing! Since we don't have a huge backlog of well-scoped projects just
yet, I recommend posting in the [discourse
group](https://scrapscript.discourse.group/) first to get an idea of what would
be most useful and also interesting to you.

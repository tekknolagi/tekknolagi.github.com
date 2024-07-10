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
        # append to the internal code buffer... more about handles later
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
{%- raw -%}
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
{% endraw -%}
```

I felt very silly after this. It's a nice showcase, though, of the advice that
people like to give about writing compilers: "just emit code that does what you
*would do* if you were in the interpreter".

The rest of the compiler is mostly code in a similar vein: translate a tree
representation of an expression to a linear series of instructions. In some
cases, lean heavily on the runtime to provide some functionality.

Speaking of the runtime...

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

struct gc_heap* make_heap(size_t size);
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

Let's talk about closures as a sample of a heap-allocated object. Each object
has a header `struct gc_obj` (which, due to the way that C compilers lay out
memory, effectively inlines it into the outer object) and then its own fields.

In the case of closures, we have a C function pointer, a count of the number of
free variables in the closure, and then a flexible-length in-line array of free
variables.

```c
struct closure {
  struct gc_obj HEAD;
  ClosureFn fn;
  size_t size;
  struct object* env[];
};
```

But not everything is heap-allocated and you may have noticed a discrepancy: we
have both `struct gc_obj` and `struct object`. What's up with that?

Well, not all pointers are pointers into the heap. Some pointers contain the
entire object inside the 64 bits of the pointer itself. In order to distinguish
the two cases in the code, we add some extra low bits to every pointer.

## Inside the runtime: tagged pointers

In Scrapscript, numbers are arbitrary-size integers. Fortunately, most numbers
are small [citation needed], and fortunately, 63 bits can represent a lot of
them.

Yes, 63, not 64. One of the pointer tagging tricks we do is bias heap-allocated
objects by 1. Since all heap allocations are multiples of 8 bytes (at least;
that's just the size of the `tag`), we know that the low three bits are
normally 0. By setting the low bits of all heap-allocated objects to 001, and
the low bit of all small integers to 0, we can distinguish between the two
cases.

```
small int:   0bxxx...xx0
heap object: 0bxxx...001
```

Actually, we can do better. We still have two more bits to play with. Using
those extra bits we can encode holes (`()`), empty lists ("nil" in other
languages), and small strings (<= 7 bytes). There's more to do here, probably,
but that's what we have so far.

Let's go back to the heap and talk about how we know which objects are live.

## Inside the runtime: handles

The usual thing to do in a Scheme-like compiler is to scan the stack and look
for things that look like pointers. If you have full control over the compiler
pipeline---in other words, you are compiling to assembly---you can do this
pretty easily. The call stack is but an array and if you know where it begins,
you can walk it to find pointers.

You run into some problems with that:

1. Even if you are not emitting C, you are often beholden to the C calling
   convention. The usual C calling conventions involve passing arguments in
   registers, and fast code tends to use registers for everything.
1. Hardware call or jump-and-link instructions push a return address onto the
   stack. This is not a pointer to a heap-allocated object but instead a
   pointer into the code stream.

The typical solution to the second problem is solved if you have tagged
pointers and don't mind aligning your code a little bit to make all return
addresses look like small integers. It's also fine if your garbage collector
does not move pointers.

The first problem requires emitting code to save and restore registers so that
the garbage collector knows the objects contained within are live---part of the
root set. If your garbage collector doesn't move pointers, you don't even need
to restore them.

If your GC *does* move pointers, both problems require fussier solutions. And
it gets even worse if you are compiling to C: your C compiler might dump random
variables onto the call stack. Whereas in assembly you have full control over
your stack layout, in C you have none.

The GC I want to use---Andy Wingo's---is a semispace collector and semispace
collectors move pointers. Let's take a look at an example to illustrate the
problems we run into.

```c
struct object* foo(struct object* x) {
  // point 0
  struct object* y = mknum(heap, kSomeBigNumber);  // point 1
  // point 2
  struct object* z = num_add(x, y);  // point 3
  return z;
}
```

At point 0 we're good. Nothing has happened in the `foo` function and we can
safely assume that the pointer `x` is valid.

Ad point 1, we allocate a number on the heap. We might have room in our current
GC space or we might not. If we don't, the collector will run and move all of
our pointers around. We can't predict at compile-time which will happen, so we
assume that everything moves.

By point 2, the `x` has become invalid. While the object *originally pointed to
by x* is still alive and well, we now have a stale/dangling/invalid pointer
into the previous GC space.

This might be fine except for the fact that at point 3, we use `x`! We need it
to still be a valid pointer. And `num_add` might cause a GC too, at which point
`y` also becomes invalid. What is there to do?

The common solution is to use a "shadow stack" or "handles". We used them in
the [Skybison](https://github.com/tekknolagi/skybison) Python runtime, Dart and
V8 use them, several JVMs use them, and so on. There have been multiple papers
([^ppdp-99], [^ismm-02], [^cases-06], [^cc-07], [^ismm-09], [^ismm-11],
probably more) written about this topic so let's use that research.

[^ppdp-99]: [C-—: A portable assembly language that supports garbage collection](/assets/img/c-minus-minus.pdf) (PDF)

[^ismm-02]: [Accurate Garbage Collection in an Uncooperative Environment](/assets/img/gc-uncooperative.pdf) (PDF)

[^cases-06]: [Supporting Precise Garbage Collection in Java Bytecode- to-C Ahead-of-Time Compiler for Embedded Systems](/assets/img/gc-jvm-bytecode-c.pdf) (PDF)

[^cc-07]: [Accurate garbage collection in uncooperative environments with lazy pointer stacks](/assets/img/gc-lazy-stack.pdf) (PDF)

[^ismm-09]: [Precise Garbage Collection for C](/assets/img/precise-gc-c.pdf) (PDF)

[^ismm-11]: [Handles Revisited: Optimising Performance and Memory Costs in a Real-Time Collector](/assets/img/gc-handles-revisited.pdf) (PDF)

The basic idea is to have your own "stack" of pointers that you know are valid
and that you can walk to find pointers into the heap. Right now it is
implemented as a linked list of chunks of pointers---where each chunk
corresponds to a C stack frame---but as I am writing this it strikes me that I
could just as easily have `mmap`ed one big linear chunk as a separate stack.

```c
#define MAX_HANDLES 20

struct handles {
  // TODO(max): Figure out how to make this a flat linked list with whole
  // chunks popped off at function return
  struct object** stack[MAX_HANDLES];
  size_t stack_pointer;
  struct handles* next;
};

static struct handles* handles = NULL;

void pop_handles(void* local_handles) {
  (void)local_handles;
  handles = handles->next;
}
```

The current shadow stack frame is always stored in the global `handles`
variable and is modified on function entry and exit. Each frame gets some
fixed number of handles (20 right now) and if you exceed that number, the
runtime aborts and the compiler developer should increase that number I guess.
That problem would go away if either I switched to the entirely linear model or
switched to a full linked list model (but I would still hold onto the previous
top of stack so I could pop all the handles at once).

To make all of this a little easier to use, I added some macros to the runtime:

```c
#define HANDLES()                                                             \
  struct handles local_handles                                                \
      __attribute__((__cleanup__(pop_handles))) = {.next = handles};          \
  handles = &local_handles

#define GC_PROTECT(x)                                                         \
  assert(local_handles.stack_pointer < MAX_HANDLES);                          \
  local_handles.stack[local_handles.stack_pointer++] = (struct object**)(&x)

#define GC_HANDLE(type, name, val)                                            \
  type name = val;                                                            \
  GC_PROTECT(name)

#define OBJECT_HANDLE(name, exp) GC_HANDLE(struct object*, name, exp)
```

This means that the compiler can generate code that looks semi-readable:

```c
struct object* foo(struct object* x) {
  HANDLES();
  GC_PROTECT(x);
  OBJECT_HANDLE(y, mknum(heap, kSomeBigNumber));
  OBJECT_HANDLE(z, num_add(x, y));
  return z;
}
```

The `HANDLES` thing is a little weird pseudo-RAII trick that a) reduces code
duplication and b) hopefully reduces errors in case of early function exits. I
trust the C compiler to correctly insert calls to `pop_handles` more than I
trust myself to.

The core idea here is to store pointers to the local `struct object*` variables
so that the C compiler is forced to read from and write to memory any time
there could be side effects. Then, when a `collect` happens, all the right
pointers in the shadow stack are visible to the garbage collector.

```c
void trace_roots(struct gc_heap* heap, VisitFn visit) {
  for (struct handles* h = handles; h; h = h->next) {
    for (size_t i = 0; i < h->stack_pointer; i++) {
      visit(h->stack[i], heap);
    }
  }
}
```

It's neat that we can add reasonably usable handles to Andy's GC in such a
small amount of code.

## Cosmopolitan and WebAssembly

While we already got Scrapscript working in the browser using the Python
interpreter and Pyodide, we can also get Scrapscript programs working---and
much smaller---by compiling the generated C code to WebAssembly. Right now it
needs access to `mmap` because of the GC but the WASI SDK provides that as long
as you specify the right compiler flags.

It's the same state of affairs with Cosmopolitan: while we used to have to
bundle the entire Python interpreter with the Scrapscript program, we can now
use `cosmocc` to compile the generated C code to produce tiny and fast
cross-platform executables. As I write this I wonder how `mmap` works on
Windows. Perhaps Cosmopolitan libc provides a `mmap` implementation that calls
`VirtualAlloc`.

## Future projects

Right now the compiler generates handles for *every local variable*. This is
not efficient, but it is correct. In the future, I would like to do a liveness
analysis and only generate handles for variables that are live across a
function that might cause a GC.

I would also like to build an intermediate representation instead of going
straight to C. This would make it easier to do local and interprocedural
optimizations. While we can do a decent amount of analysis and optimization
directly on the AST, I think it's much easier to do it on a more linear IR.

With this IR, I would like to do a mix of classic Scheme/ML and other SSA
optimizations. Ideally we can make Scrapscript fly.

The previous post mentioned a notion of "platforms" like the web platform.
Compiling Scrapscript to C would make for pretty easy interoperation with
existing libraries... for example, say, a little web server. I'm on the hunt
for a small, fast, and easy-to-use web server library that I can bundle with a
compiled Scrapscript program and have Scrapscript interact with the outside
world.

## Playing with the compiler

Try running `./scrapscript.py compile --compile examples/0_home/factorial.scrap` which
will produce both `output.c` and `a.out`. Then you can run `./a.out` to see the
result of your program.

## Thanks for reading

Want to learn more? Well first, play with [the web REPL](https://scrapscript.fly.dev/repl). Then
take a look at [the repo](https://github.com/tekknolagi/scrapscript) and start
contributing! Since we don't have a huge backlog of well-scoped projects just
yet, I recommend posting in the [discourse
group](https://scrapscript.discourse.group/) first to get an idea of what would
be most useful and also interesting to you.

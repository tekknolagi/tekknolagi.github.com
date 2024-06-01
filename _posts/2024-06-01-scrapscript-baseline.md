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

## Thanks for reading

Well first, play with [the web REPL](https://scrapscript.fly.dev/repl). Then
take a look at [the repo](https://github.com/tekknolagi/scrapscript) and start
contributing! Since we don't have a huge backlog of well-scoped projects just
yet, I recommend posting in the [discourse
group](https://scrapscript.discourse.group/) first to get an idea of what would
be most useful and also interesting to you.

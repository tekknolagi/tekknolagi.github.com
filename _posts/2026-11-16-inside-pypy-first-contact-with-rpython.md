---
title: "Inside PyPy: first contact with RPython"
layout: post
---

I get restless now and then and I want to implement a programming language.
Then after mustering the resolve, I stew, for there is no good implementation
language.[^come-at-me] What am I to do, build my own programming language so
that I can then use it to implement the first language? So I go back to
modifying an existing implementation.

[^come-at-me]: I don't want to implement atop Racket. C++ and C give me endless
    nightmares and are completely DIY. Yes, yes, I have built large VMs on C++
    and C before. One time I posted so hard about the frustration that my tech
    lead unfollowed me on Twitter. I don't really want to try to think about GC
    in Rust.

There do exist established general-purpose tools for building fast programming
language implementations:

* RPython, which has PyPy (Python) as its primary success story but also can
  claim Topaz (Ruby) and a couple of other smaller ones
* Truffle, which is the foundation of TruffleRuby, GraalPython, GraalJS, and
  even Sulong (LLVM)

And more nascent ones such as [Deegen][], which was used to reimplement Lua.

[Deegen]: https://github.com/luajit-remake/luajit-remake

They each take different approaches:

* Deegen requires you to write your bytecode interpreter and type system in a
  C++ DSL. From this it generates an interpreter, a baseline JIT compiler, and,
  hopefully eventually, an optimizing JIT compiler.
* Truffle requires you to write an AST or (newly) bytecode interpreter in an
  Annotation-based Java DSL. From this it specializes guest language code
  before emitting Graal IR, which uses aggressive partial evaluation to
  generate optimized code.
* RPython requires you to write an interpreter in a restricted dialect of
  Python 2 that acts more like C than Python. The RPython compiler will then
  analyze your code, translate your "Python" program into C code bundled with
  RPython IR, and then "tier up" from the C code to JITed code at run-time. In
  order to go from "random RPython program" to "JIT compiler", you need to
  participate in its *meta-tracing*, which requires annotating where your
  interpreter loop loops.

While none of these *spark joy*, they do each have benefits, including
accomplishing the primary goal of offloading a lot of the language runtime
choices and nasty details.

In this post we will take a first look at RPython because it is the kind of
VM implementor's programming language that I keep thinking about. More on that
later.

Before we continue, please heed this warning: RPython is an implementation
detail of PyPy. It has not had the years of polish that the rest of the system
has had. Please do not badger maintainers.

## A little Python

We're going to build a little program in RPython. I am mostly following along
with [the docs](https://rpython.readthedocs.io/en/latest/), specifically
[Getting Started with
RPython](https://rpython.readthedocs.io/en/latest/getting-started.html).

```python
# double.py
def double(x):
    return x + x

def entry_point(argv):
    print double(3)
    return 0

if __name__ == "__main__":
    import sys
    sys.exit(entry_point(sys.argv))
```

There's a little more ceremony than you might be used to and that's because
we're going to run this file in two modes: normal Python 2 interpreter and
compiled to C. This is the weirdness of RPython.

Run it:

```
$ python2 double.py
```

If you don't have `python2` installed, you can install PyPy, which helpfully
still implements Python 2.

If this all went fine, you will see it print `6`.

None of this is RPython *yet*. For that, we need a hint more ceremony.

## First contact

This is when we clone the PyPy project, in which we will find RPython.

```
$ git clone --depth=1 https://github.com/pypy/pypy
```

Now edit the Python program. Below the `entry_point` definition, add the
following function:

```python
def target(*args):
    return entry_point
```

This `target` function is apparently unused, but it is actually a hint to
RPython: this is where the compiler should start translating.

Either inside the PyPy directory or with `PYTHONPATH=/path/to/pypy`, run:

```
$ python2 /path/to/pypy/rpython/bin/rpython double.py
```

If all goes well, you will see a whole mess of output, including a partially
rendered Mandelbrot fractal. Finally, you will see something like:

```
...
[Timer] Timings:
[Timer] loading target                 ---  0.0 s
[Timer] annotate                       ---  0.5 s
[Timer] rtype_lltype                   ---  0.5 s
[Timer] backendopt_lltype              ---  0.3 s
[Timer] stackcheckinsertion_lltype     ---  0.0 s
[Timer] database_c                     --- 10.6 s
[Timer] source_c                       ---  1.2 s
[Timer] compile_c                      ---  1.5 s
[Timer] =========================================
[Timer] Total:                         --- 18.0 s
$
```

This is the RPython compilation process. In it, RPython is analyzing types,
removing memory allocations, monomorphizing functions, and generating C code.
Then it hands this C code off to a C compiler to compile.

You should have in your current directory an executable named `double-c`, which
you can run like `./double-c`. It should also print `6`.

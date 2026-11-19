---
title: "Inside PyPy: first contact with RPython"
layout: post
---

I get restless now and then and I want to implement a programming language.
Then after mustering the resolve, I stew, for there is no good implementation
language.[^come-at-me] What am I to do, build my own programming language so
that I can then use it to implement the first language? So I go back to
modifying an existing implementation instead.

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
  Python 2 that acts more like Java or Go than Python. The RPython compiler will then
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

This `target` function looks unused, but it is actually a hint to RPython: this
is where the compiler should start translating.

Let us now compile this program. Either inside the PyPy directory or with
`PYTHONPATH=/path/to/pypy`, run:

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

So far, this is not so different from Python. But let's try something Pythonic.
Let's see what happens when we also use `+` to mean string concatenation.

```python
def double(x):
    return x + x

def entry_point(argv):
    print double("a")
    print double(3)
    return 0

def target(*args):
    return entry_point

if __name__ == "__main__":
    import sys
    sys.exit(entry_point(sys.argv))
```

Under normal Python 2, this will work just fine, printing `aa` and then `6`.
Under RPython, though, you get this inscrutable error (again, rough edges):

```
[translation:ERROR] UnionError:

Offending annotations:
  SomeChar(const='a', no_nul=True)
  SomeInteger(const=3, knowntype=int, nonneg=True, unsigned=False)


Occurred processing the following simple_call:
  function double <double.py, line 1> returning

    v2 = simple_call((function double), (3))

In <FunctionGraph of (double:4)entry_point at 0x59c0ad837b40>:
Happened at file double.py line 6

        print double("a")
==>     print double(3)

Known variable annotations:

Processing block:
 block@6[argv_0] is a <class 'rpython.flowspace.flowcontext.SpamBlock'>
 in (double:4)entry_point
 containing the following operations:
       v3 = simple_call((function double), ('a'))
       v4 = str(v3)
       v5 = simple_call((function rpython_print_item), v4)
       v6 = simple_call((function rpython_print_newline))
       v2 = simple_call((function double), (3))
       v7 = str(v2)
       v8 = simple_call((function rpython_print_item), v7)
       v9 = simple_call((function rpython_print_newline))
```

and are unceremoniously dropped into the debugger, Pdb (hit Ctrl-D or type
`exit`).

From this message you are meant to intuit that RPython did not enjoy that two
different types flowed into the `double` function. It helpfully tells you that
there was a `UnionError` when trying to reconcile the `SomeChar` and
`SomeInteger` types that flowed into the function's first parameter. It then
gives you a pointer to the offending line and an IR dump.

This happens because RPython requires function parameters to be monomorphic.
Though the variables are syntactically untyped, RPython computes a type
analysis to discover what type each variable should be. When `"a"` and `3` both
flow into `x`, boom, complaint.[^variables]

[^variables]: I initially thought any variable had to be monomorphic but I
    could not get an error from programs such as `x = "a"; x = 3`.

To get around this, we do what all good C++ programmers do and we templatize
the function. The RPython spelling of this is, as with many other features, a
function annotation:

```python
from rpython.rlib.objectmodel import specialize

@specialize.argtype(0)
def double(x):
    return x + x

# ...
```

You will find now that RPython happily compiles the program. This works because
it makes a new copy of `double` for every input type of the first (`0`)
argument.

"Hey wait a minute,", you might ask, "doesn't this mean it stops being a valid
Python 2 program?"

Great question. No.

You can still run the program as long as you provide the location of the
RPython libraries via `PYTHONPATH` (as before):

```
$ PYTHONPATH=/path/to/pypy pypy double.py
aa
6
$
```

This runs ~instantly, which is a big relief. Developing an interpreter project
with a 30 second or more turnaround time for each compile would be really
frustrating.

This has been a taste of RPython, both great and somewhat frustrating. I
intentionally made it about RPython *as a programming language* rather than
being so focused on RPython *as a programming language implementation tool*
because I want some of its features to exist in a general purpose programming
language... that then also might be used to implement other programming
languages.

I bring it up because I want to rehash the stuff that I occasionally grumble
about in the vicinity of PLDI but for some reason have not written about
online. Perhaps writing about it will encourage people.

## A new (meta-)programming toolkit

I think there is a lot of good stuff in RPython:

* Compile to native AOT with additional JIT. Seems like this provides good
  start-up performance with (optional!) tier-up as the runtime system observes
  the system's behavior.
* Automatic memory optimization. There is no good way to syntactically express
  how an object should be allocated---Python is, after all, a very high-level
  language---so Python programs are profligate allocators of memory. RPython
  programs *would be* the same except that the AOT compiler does a
  whole-program analysis to try and remove these allocations. The JIT does too,
  but via a different mechanism.
* The JIT is provided to you. You don't have to think about program analysis;
  the PyPy authors think about it for you.
* It is possible to do manual memory layout via `lltype` so that you can write
  better data structures than Python would otherwise let you express.
* It provides a garbage collector for you. Furthermore, it handles all the
  stack scanning stuff and "oop maps" so you don't have to build
  handles/`GC_PROTECT`/... yourself.

I wish some stuff were different, or at least modifiable:

* RPython does its partial evaluation and automatic JIT creation via
  *meta-tracing*. It is one of the last surviving tracing JIT compilers
  (alongside LuaJIT and, kind of, HHVM). For a lot of programs, method-based
  compilers seem to have produced better, or at least more predictable,
  results. I kind of want to see what a meta-method-JIT would look like.
* The user experience leaves something to be desired. I want better error
  messages, faster compiles, and less output. I want thorough docs.
* I want better performance feedback. This is in line with the rest of my
  ramblings about mechanical sympathy: the runtime should tell you what is
  wrong. I (or CF) shouldn't have to stare at IR in traces and go "hmmmmm..."
* It requires writing a lot of your tooling anew. Even if I had, for example, a
  JavaScript parser written in Python, it likely wouldn't be in Python 2 and it
  likely would not work well with RPython's special rules. Runtimes such as
  JRuby get around this by compiling parsers (Prism) to WebAssembly and then
  bundling a WebAssembly interpreter written in Java.
* Along these lines, I would like more help with lexing and parsing, for which
  I have so little patience.
* I think it would be neat if it had a way to pause and save an "image" of the
  heap. After all, RPython is in control of all the memory and the code.
* It would be neat if, in addition to a JIT, it also helped you build an AOT
  compiler for your guest language.
* What if your language is statically typed? Should the toolkit provide
  functionality for type checking? Type inference?
* Some solution for C code. You can call it and stuff just fine but there's no
  Sulong equivalent so it's a totally opaque barrier to optimization. Don't
  write your runtime helpers in C, sure, but at some point you have to interact
  with outside code.

I will probably add more to this list as I continue to remember some years of
these conversations.

Wouldn't this be great?

## Now what?

I don't really have the time, energy, or expertise to build such a thing. There
are so many design decisions along the way. It would probably accidentally grow
to be huge and ossify along the way, and what a bummer that would be.

If you have any pointers to existing projects or a way to implement such a
thing in a really lightweight form (read: sub 10k lines, portable ideas), I
would be very interested.

Here are some pointers to other projects in the neighborhood:

* Weval, which turns an interpreter into a baseline AOT compiler. Its current
  implementation requires the interpreter to be compilable to Wasm and then its
  heap snapshottable with Wizer. One can imagine this technique being used
  instead to implement a JIT instead of an AOT, thereby also sidestepping the
  snapshot requirement. One might also imagine this applying to another IR than
  WebAssembly. Say, LLVM...
* Yk, which is another tracing framework that you can add to an existing
  interpreter. It traces and optimizes LLVM IR.
* semPy, which is an executable semantics for Python.
* Zig and other languages such as the more nascent k1 have the ability to run
  and generate code at build-time. Perhaps someone could implement a VM toolkit
  in one of these languages. I think a lot of the effort would still go toward
  something like GC handles if you want a moving GC.
* Virgil
* Mojo

## Thank you

Thanks for CF Bolz-Tereick first for allowing me to talk their ear off about
this for years and second for reviewing this post.

---
title: "Compiling typed Python"
layout: post
description: With a little effort, you can make your mypy-typed Python go zoom.
date: 2023-06-19
---

It's been nine whole years since [PEP 484](https://peps.python.org/pep-0484/)
landed and brought us types from on high. This has made a lot of people very
angry and been widely regarded as a bad move[^adams]. Since then, people on the
internet have been clamoring to find out: [does this mean we can now compile
Python](https://discuss.python.org/t/cpython-optimizations-leveraging-type-hints-bachelor-thesis-topic/27264)
[to native code for more speed](https://stackoverflow.com/questions/40204951/python-3-type-hints-for-performance-optimizations)?
It's a totally reasonable question. It was one of my first questions when I
first started working on Python compilers. So can we do it?

[^adams]: According to Douglas Adams, anyway.

No. But also, kind of, yes. I'll explain. I'll explain in the context of
"ahead-of-time" compiling within or adjacent to CPython, the predominant
implementation of the Python language. Just-in-time (JIT) compilers are a
different beast, and are described more below. None of the information in this
post is novel; I hope only to clarify a bunch of existing academic and industry
knowledge.

The core thesis is: *types are very broad hints and they are sometimes lies*.

## It's not what you think

A lot of people enjoy walking into discussions and saying things like "We have
a 100% Mypy/Pyright/Pytype/Pyre typed codebase. I would simply use the types in
compilation to generate better code." That was the original title of this
article, even. "*I would simply use the types in the compiler.*" But it doesn't
really work like that[^simple-annotations]. I'll show a couple examples that
demonstrate why. The point is not to browbeat you with example after example;
the point is to get people to stop saying "just" and "simply".

[^simple-annotations]: I'm not even going to address fancy type-level
    metaprogramming stuff, because I don't think people ever intended to use
    that for performance anyway. There are [lots of
    uses](https://lukeplant.me.uk/blog/posts/the-different-uses-of-python-type-hints/)
    for Python type annotations. We'll just look at very simple bog-standard
    `x: SomeTypeName` annotations.

For example, look at this lovely little Python function. It's short, typed, and
obviously just adds integers. Easy: compiles right down to two machine
instructions. Right?

```python
def add(x: int, y: int) -> int:
    return x + y

add(3, 4)  # => 7
```

Unfortunately, no. Type annotations of the type `x: T` mean "`x` is an instance
of `T` or an instance of a subclass of `T`."[^exact] This is pretty common in
programming languages, and, thanks to [Barbara
Liskov](https://en.wikipedia.org/wiki/Liskov_substitution_principle), makes
semantic sense most of the time. But it doesn't help performance here. The
dispatch for binary operators in Python is famously [not
simple](https://snarky.ca/unravelling-binary-arithmetic-operations-in-python/)
because of subclasses. This means that there is a lot of code to be executed if
`x` and `y` could be any subclasses of `int`.

[^exact]: The Static Python project sort of has an internal `Exact` type that
    can be used like `x: Exact[int]` to disallow subclasses, but it's not
    exposed. This is, as Carl Meyer explained to me, because Mypy, Pyre, and
    other type checkers don't have a good way to check this type; the Python
    type system does not support this.

```python
class C(int):
    def __add__(self, other):
        send_a_nasty_email()  # sigh
        return 42
```

If this sounds familiar, you might have read the [Ben-Asher and Rotem
paper](https://dl.acm.org/doi/10.1145/1534530.1534550), [Falcon
paper](https://arxiv.org/abs/1306.6047), or [Barany
paper](https://dl.acm.org/doi/10.1145/2617548.2617552), all of which discuss
how everything in Python basically boils down to a function call and that makes
it hard to statically analyze. This post is subtly different because we now
have types, and people assume those make the analysis easier.

Arbitrary code execution aside, it's also not obvious how the appropriate
`__add__` function is selected in operator dispatch. **You don't need to
understand or really even read the big blob** that explains it below. You just
need to say "ooh" and "aah" and "wow, so many if-statements."

```python
_MISSING = object()

def sub(lhs: Any, rhs: Any, /) -> Any:
        # lhs.__sub__
        lhs_type = type(lhs)
        try:
            lhs_method = debuiltins._mro_getattr(lhs_type, "__sub__")
        except AttributeError:
            lhs_method = _MISSING

        # lhs.__rsub__ (for knowing if rhs.__rsub__ should be called first)
        try:
            lhs_rmethod = debuiltins._mro_getattr(lhs_type, "__rsub__")
        except AttributeError:
            lhs_rmethod = _MISSING

        # rhs.__rsub__
        rhs_type = type(rhs)
        try:
            rhs_method = debuiltins._mro_getattr(rhs_type, "__rsub__")
        except AttributeError:
            rhs_method = _MISSING

        call_lhs = lhs, lhs_method, rhs
        call_rhs = rhs, rhs_method, lhs

        if (
            rhs_type is not _MISSING  # Do we care?
            and rhs_type is not lhs_type  # Could RHS be a subclass?
            and issubclass(rhs_type, lhs_type)  # RHS is a subclass!
            and lhs_rmethod is not rhs_method  # Is __r*__ actually different?
        ):
            calls = call_rhs, call_lhs
        elif lhs_type is not rhs_type:
            calls = call_lhs, call_rhs
        else:
            calls = (call_lhs,)

        for first_obj, meth, second_obj in calls:
            if meth is _MISSING:
                continue
            value = meth(first_obj, second_obj)
            if value is not NotImplemented:
                return value
        else:
            raise TypeError(
                f"unsupported operand type(s) for -: {lhs_type!r} and {rhs_type!r}"
            )
```

This enormous code snippet is from Brett Cannon's linked post above. It
demonstrates in Python "pseudocode" what happens in C under the hood when doing
`lhs - rhs` in Python.

But let's ignore this problem and pretend that it's not possible to subclass
`int`. Problem solved, right? High performance math?

Unfortunately, no. While we would have fast dispatch on binary operators and
other methods, integers in Python are heap-allocated big integer objects. This
means that every operation on them is a function call to `PyLong_Add` or
similar. While these functions have been optimized for speed over the years,
they are still slower than machine integers. But let's assume that a
sufficiently smart compiler can auto-unbox small-enough big integers into
machine words at the beginning of a function. We can do fast math with those.
If we really want, we can even do fast floating point math, too. Problem
solved? Hopefully?

```python
# You can have fun making enormous numbers! This is part of the Python
# language.
def big_pow(x: int) -> int:
    # Even if you unbox `x` here, the result might still be enormous.
    return 2**x
```

Unfortunately, no. Most math kernels are not just using built-in functions and
operators. They call out to external C libraries like NumPy or SciPy, and those
functions expect heap-allocated `PyLongObject`s. The C-API is simply **not
ready** to expose the underlying functions that operate on machine integers,
and it is also **not ready** for [tagged pointers](/blog/small-objects/). This
would be a huge breaking change in the API and ABI. But okay, let's assume for
the sake of blog post that the compiler team has a magic wand and can do all of
this.

```c
// Store 63-bit integers inside the pointer itself.
static const long kSmallIntTagBits = 1;
static const long kBits = 64 - kSmallIntTagBits;
static const long kMaxValue = (long{1} << (kBits - 1)) - 1;

PyObject* PyLong_FromLong(long x) {
    if (x < kMaxValue) {
        return (PyObject*)((unsigned long)value << kSmallIntTagBits);
    }
    return MakeABigInt(x);
}

PyObject* PyLong_Add(PyObject* left, PyObject* right) {
    if (IsSmallInt(left) && IsSmallInt(right)) {
        long result = Untag(left) + Untag(right);
        return PyLong_FromLong(result);
    }
    return BigIntAdd(left, right);
}
```

Then we're set, right?

Unfortunately, there are still some other loose ends to tie up. While you may
have a nice and neatly typed numeric kernel of Python code, it has to interact
with the outside world. And the outside world is often not so well typed. Thank
you to Jeremy Siek and Walid Taha for giving us [gradual
typing][gradual]---this is the reason anything gets typed at all in
Python---but you can't do type-driven compilation of untyped code and expect it
to be fast.

[gradual]: https://wphomes.soic.indiana.edu/jsiek/what-is-gradual-typing/

This means that at the entry to your typed functions, you have to check the
types of the input objects. Maybe you can engineer a system such that a
function can have multiple entry points---one for untyped calls, one for typed
calls, and maybe even one for unboxed calls---but this hypothetical system's
complexity is growing, and fast. And there are a whole host of other
complications and bits of dynamic behavior that I haven't even mentioned.

```python
def typed_function_unboxed(x: int64, y: int64) -> int64:
    return int64_add(x, y)

def typed_function(x: int, y: int) -> int:
    return typed_function_unboxed(unbox(x), unbox(y))

def typed_function_shell(x, y):
    if not isinstance(x, int):
        raise TypeError("...")
    if not isinstance(y, int):
        raise TypeError("...")
    return typed_function(cast(x, int), cast(y, int))

f = make_typed_function(typed_function_shell, typed_function, typed_function_unboxed)
f(3, 4)  # The dispatch gets hairy
```

And it's not just about types, either! For example, variable binding,
especially global variable binding, is a performance impediment. Globals, even
builtins, are almost always overwritable by any random Python code.

"But Max," you say, "Python compiler libraries like Numba clearly work just
fine. Just-in-time compilers have been doing this for years. What's the deal?"

Just-in-time compilers can be so effective because they can speculate on things
that are not compile-time constants. Then, if the assumption is no longer true,
they can fall back on a (slower) interpreter. This is how
[PyPy](https://www.pypy.org/), the successor of
[Psyco](https://psyco.sourceforge.net/), does its amazing work specializing
data structures; the JIT does not have to handle every case. This interpreter
deoptimization is part of what makes JITs hard to understand and does not much
help with compiling code ahead-of-time.

<!-- TODO: Address Brett's comments:

I'm not sure how actionable this is, but there's nothing stopping an AOT
compiler from having speculative fast paths. And when you collect and save
profile data, you can blue the line between AOT and JIT compilation even more.

TODO: apparently SubstrateVM supports this to some extent, too
-->

This is what a PyPy trace might look like, taken from the 2011 paper [Runtime
Feedback in a Meta-Tracing JIT for Efficient Dynamic
Languages](https://dl.acm.org/doi/10.1145/2069172.2069181). Every `guard` is a
potential exit from JITed code to the interpreter:

```python
# inst1.getattr("a")
map1 = inst1.map
guard(map1 == 0xb74af4a8)
index1 = Map.getindex(map1, "a")
guard(index1 != -1)
storage1 = inst1.storage
result1 = storage1[index1]
```

And the other thing is, unlike PyPy, Numba doesn't exactly compile *Python*...

## Dialects

Numba is great! I cannot emphasize enough how much I am **not** trying to pick
on Numba here. The team put in a lot of hard work and solid engineering and
built a compiler that produces very fast numerical code.

It's possible to do this because Numba compiles a superficially similar
language that is much less dynamic and is focused on numerics. For people who
work with data analytics and machine learning, this is incredible!
Unfortunately, it doesn't generalize to arbitrary Python code.

This is also true of many other type-driven compiler projects that optimize
"Python" code:

* [Cython](https://github.com/cython/cython)/[Pyrex](https://www.csse.canterbury.ac.nz/greg.ewing/python/Pyrex/)
* [MicroPython Viper](https://docs.micropython.org/en/v1.9.3/pyboard/reference/speed_python.html#the-viper-code-emitter)
* [Mojo](https://www.modular.com/mojo)
* [Mypyc](https://github.com/mypyc/mypyc)
* [Shed Skin](https://github.com/shedskin/shedskin)
* [Starkiller](http://michael.salib.com/writings/thesis/thesis.pdf) (PDF)
* [Static Python](https://github.com/facebookincubator/cinder/#static-python)
* [Typed Python](https://github.com/APrioriInvestments/typed_python/blob/dev/docs/introduction.md)

and in particular to optimize numerics:

* [Codon](https://github.com/exaloop/codon)
* [Numba](https://github.com/numba/numba)
* [LPython](https://github.com/lcompilers/lpython)
  (more info in [blog post](https://lpython.org/blog/2023/07/lpython-novel-fast-retargetable-python-compiler/))
* [Pyccel](https://github.com/pyccel/pyccel/blob/devel/tutorial/quickstart.md)
* [Pythran](https://pythran.readthedocs.io/en/latest/)
* [Taichi](https://github.com/taichi-dev/taichi)
* [TensorFlow JIT/XLA](https://www.tensorflow.org/xla)
* [TorchScript](https://pytorch.org/docs/stable/jit_language_reference.html)

and probably more that I forgot about or could not find (please feel free to
submit a PR).

It does raise an interesting question, though: what if you intentionally and
explicitly eschew the more dynamic features? Can you get performance in return?
It turns out, yes. Absolutely yes.

## Stronger guarantees

As people have continually rediscovered over the years, Python is hard to
optimize statically. Functions like the following, which "only" do an attribute
load, have no hope whatsoever of being optimized out of context:

```python
def get_an_attr(o):
    return o.value
```

This is because `o` could be any object, types can define custom attribute
resolution by defining a `__getattr__` function, and therefore attribute loads
are equivalent to running opaque blobs of user code.

Even if the function is typed, you still run into the subclass problem
described earlier. You also have issues like instance attributes, deleting
attributes, descriptor shadowing, and so on.

```python
class C:
    def __init__(self):
        self.value = 5


def get_an_attr(o: C):
    return o.value
```

*However*, if you intentionally opt out of a lot of that dynamism, things start
getting interesting. The [Static
Python](https://github.com/facebookincubator/cinder/#static-python) compiler,
for example, is part of the Cinder project, and lets you trade dynamism for
speed. If a module is marked static with `import __static__`, Cinder will swap
out the standard bytecode compiler for the Static Python bytecode compiler,
which *compiles a different language*!

By way of example, the SP compiler will automatically slotify all of the
classes in a SP module and prohibit a `__dict__` slot. This means that features
that used to work---dynamically creating and deleting attributes, etc---no
longer do. But it also means that attribute loads from static classes are
actually only three machine instructions now. This tradeoff makes the compiler
transform *sound*. Most people like this tradeoff.

```python
import __static__

class C:
    def __init__(self):
        self.value: int = 5

def get_an_attr(o: C):
    return o.value
# ...
# mov rax,QWORD PTR [rsi+0x10]  # Load the field
# test rax,rax                  # Check if null
# je 0x7f823ba17283             # Maybe raise an exception
# ...
```

Static Python does this just with existing Python annotations. It has some more
constraints than Mypy does (you can't `ignore` your type errors away, for
example), but it does not change the syntax of Python. But it's important to
know that this does not just immediately follow from a type-directed
translation. It requires opting into stricter checking and different behavior
for an entire typed core of a codebase---potentially gradually (SP can call
untyped Python and vice versa). It requires changing the runtime representation
of objects from header+dictionary to header+array of slots. For this reason it
is (currently) implemented as a custom compiler, custom bytecode interpreter,
and with support in the Cinder JIT. To learn more, check out the Static Python
team's [paper collaboration with Brown](https://cs.brown.edu/~sk/Publications/Papers/Published/lgmvpk-static-python/),
which explains a bit more about the gradual typing bits and soundness.

I would be remiss if I did not also mention
[Mypyc](https://github.com/mypyc/mypyc) (the optimizer and code generator for
Mypy). Mypyc is very similar to Static Python in that it takes something that
looks like Python with types and generates type-optimized code. It is different
in that it generates C extensions and does [tagged pointers for
integers](https://mypyc.readthedocs.io/en/latest/int_operations.html) by
default. Depending on your use case---in particular, your deployment story---it
may be the compiler that you want to use! The Black formatter, for example, has
had [great
success](https://ichard26.github.io/blog/2022/05/compiling-black-with-mypyc-part-1/)
using Mypyc[^subtle-differences].

[^subtle-differences]: Dialects often have subtle and unexpected differences.
    The differences are not always necessarily related to type checks;
    sometimes they change how variables are bound. For example, Mypyc changes
    the behavior of imports and global variables. Consider these three files:
    ```python
    # a.py
    def foo():
      return 123

    def override():
      return 456

    foo = override
    ```
    ```python
    # b.py
    from a import foo

    def bar():
      return foo()
    ```
    ```python
    # main.py
    from b import bar

    print(bar())
    ```
    First run with `python3 main.py`. Then try running `python3 -m mypyc a.py
    b.py` (I have `mypy 1.4.0 (compiled: yes)`) and then `python3 main.py`
    again.

    In normal Python semantics, the result printed is 456. However, with Mypyc,
    the result is 123. Something about global variable binding for functions in
    Mypyc-Mypyc imports is different; with just two files (one Mypyc compiled,
    the other a main), the behavior is as expected.

    Maybe this is intended or at least documented behavior on the part of the
    Mypyc authors, and maybe it is not. Either way, faithfully reproducing the
    full Python semantics is very difficult.

I mentioned earlier that Static Python has some additional rules for typing
that Mypy does not, and that Mypyc is based on Mypy. They both try to be
correct compilers and optimizers, so how do they work around users lying to the
type checker with `type: ignore`?

```python
def foo(x: int) -> int:
    return x

foo("hello")  # type: ignore
```

In this example,

* CPython ignores the annotations completely in compilation and execution
* Mypy lets the type mismatch slide due to `type: ignore`
  * Which, interestingly enough, means Mypyc defers the problem to run-time by
    injecting an `unbox` that might cleanly raise a `TypeError`
* Static Python does not allow this code to compile
  * Note that non-Static Python code in the same project would not be
    compile-time checked and therefore would only get a run-time `TypeError` at
    the boundary if they called `foo` with a non-`int`

All of these are reasonable behaviors because each project has different goals.

In addition to the normal types available, both Static Python and Mypyc allow
typing parameters and other variables as primitive ints like `int8` so you can
get the unboxed arithmetic that people tend to expect on first reading of the
first code snippet in this post[^finality].

[^finality]: Implicit here is that these primitive int types are final, which
    means that annotations like `a: int8` can be trusted. There is no subclass
    of `int8` so we can optimize!

Other projects take this further. The [Mojo](https://www.modular.com/mojo)
project, for example, aims to create a much bigger and more visibly different
new language that is a proper superset of Python[^chris]. The Python code it
runs should continue to work as advertised, but modules can opt into less
dynamism by iteratively converting their Python code to something that looks a
little different. They also do a bunch of other neat stuff, but that's outside
the scope of this blog post.

[^chris]: Thank you to [Chris Gregory](https://www.chrisgregory.me/) for
    pushing me to look more at it!

See for example this snippet defining a struct (a new feature) that reads like
a Python class but has some stronger mutability and binding guarantees:

```python
@value
struct MyPair:
    var first: Int
    var second: Int
    def __lt__(self, rhs: MyPair) -> Bool:
        return self.first < rhs.first or
              (self.first == rhs.first and
               self.second < rhs.second)
```

It even looks like the `@value` decorator gives you value semantics.

Per the Mojo [docs](https://docs.modular.com/mojo/programming-manual.html#struct-types),

> Mojo structs are static: they are bound at compile-time (you cannot add
> methods at runtime). Structs allow you to trade flexibility for performance
> while being safe and easy to use.
>
> [...]
>
> In Mojo, the structure and contents of a "struct" are set in advance and
> can't be changed while the program is running. Unlike in Python, where you
> can add, remove, or change attributes of an object on the fly, Mojo doesn't
> allow that for structs. This means you can't use `del` to remove a method or
> change its value in the middle of running the program.

Seems neat. We'll see what it looks like more when it's open sourced.

And finally, even if you are not trying to do optimized code generation,
packaging up all the code at app bundle time can help save big on runtime
startup. If you don't need to hit the disk at least once per imported module,
you get some big wins in time and code locality. *ngoldbaum* on lobste.rs
[notes](https://lobste.rs/s/lnyfm6/compiling_typed_python#c_kifkr4) that
[PyOxidizer](https://github.com/indygreg/PyOxidizer) can bundle your code into
the data segment of an executable.

This already happens with the [frozen built-in
modules](https://docs.python.org/3.11/whatsnew/3.11.html#faster-startup) (was
just `importlib` before 3.11) and has been tried before with entire
applications
([one](https://mail.python.org/pipermail/python-dev/2018-May/153367.html),
[two](https://bugs.python.org/issue36839),
[three](https://github.com/faster-cpython/ideas/discussions/150)
(productionized [here](https://github.com/alibaba/code-data-share-for-python)),
and maybe others) with varying upstreaming success.

## Other approaches

[Nuitka](https://github.com/Nuitka/Nuitka) (not mentioned above) is a
whole-program compiler from Python to C. As far as I can tell, it does not use
your type annotations in the compilation process. Instead it uses its own
optimization pipeline, including function inlining, etc, to discover types.
Please correct me if I am wrong!

The [Oils project](http://www.oilshell.org/) includes a shell written in Python
that is accelerated by a Python-to-C++ compiler called
[mycpp](https://www.oilshell.org/blog/2022/05/mycpp.html). This is not a
general-purpose compiler; Andy's perspective on this is that they only ever
intend to be able to compile and run *one codebase*. This lets them completely
ignore very dynamic Python semantics (which the Oil shell does not use) as long
as the singular end-user program has the same visible behavior.

### In other languages

If you don't like this whole "typed kernel" idea, other compilers like Graal's
[SubstrateVM](https://docs.oracle.com/en/graalvm/enterprise/20/docs/reference-manual/native-image/#native-image)
do some advanced wizardry to analyze your whole program.

SubstrateVM is an ahead-of-time compiler for Java that looks at your entire
codebase as a unit. It does some intense inter-procedural static analysis to
prove things about your code that can help with performance. It also subtly
changes the language, though. In order to do this analysis, it prohibits
arbitrary loading of classes at runtime. It also limits the amount of
reflection to some known feature subset.

## What this means for the average Python programmer

If you are working on a science thing or machine learning project, you most
likely have a bunch of glue around some fast core of hardcore math. If you add
types and use one of the excellent compilers listed above, you will probably
get some performance benefits.

If you are working on some other project, though, you may not have such a clear
performance hotspot. Furthermore, you may not be working with objects that have
fast primitive representations, like `int`. You probably have a bunch of data
structures, etc. In that case, some of the projects above like Mypyc and
Static Python and Nuitka can probably help you. But you will need to add types,
probably fix some type errors, and then change how you build and run your
application.

Unfortunately for everybody who writes Python, a huge portion of code that
needs to be fast is written as C extensions because of historical reasons. This
means that you can optimize the Python portion of your application all you
like, but the C extension will remain an opaque blob (unless your compiler
understands it like Numba understands NumPy, that is). This means that you may
need to eventually re-write your C code as Python to fully reap all the
performance benefits. Or at least [type the
boundary](https://github.com/faster-cpython/ideas/issues/546).

## What this means for other dynamic languages

These questions about and issues with straightforward type-driven compilation
are not unique to Python. The Sorbet team had to do [a lot of
work](https://sorbet.org/blog/2021/07/30/open-sourcing-sorbet-compiler) to make
this possible for Ruby. People ask the same thing about typed JavaScript (like
TypeScript) and probably other languages, too. People will have to work out
similar solutions. I am looking forward to Tzvetan Mikov's talk on
[Static Hermes](https://twitter.com/react_native_eu/status/1672143102662918144) at
React Native EU 2023 (slides are [here](https://speakerdeck.com/tmikov2023/static-hermes-react-native-eu-2023-announcement)
and blog post is [here](https://tmikov.blogspot.com/2023/09/how-to-speed-up-micro-benchmark-300x.html)).

There is also Manuel Serrano's whole-program [AOT
compiler](https://www-sop.inria.fr/members/Manuel.Serrano/publi/serrano-dls18.pdf)
(PDF) for JS, called Hopc. It will ignore your type annotations and do its own
analysis, though. Serrano notes:

> Hint types are unsound as they do not denote super sets of all the possible
> values variables can hold at runtime, neither they abstract all possible
> correct program executions.
>
> [...]
>
> Type information alone is not enough to generate fast code. For that, the
> compilation chain has to include all sorts of optimizations such as inline
> property caches, closure allocation, function inlining, data-flow analysis,
> register allocation, etc.

Seems about right.

<!-- TODO: mention https://github.com/natalie-lang/natalie and how it handles
types (if it does?) -->

## Wrapping up

Types are not ironclad guarantees of data layout. Changing the language you are
compiling to prohibit certain kinds of dynamism can help you with performance.
Several projects already do this and it seems to be growing in popularity.

If nothing else, I hope you have more of an understanding of what types mean
from a correctness perspective, from a performance perspective, and how they do
not always overlap.

I also hope you don't come away from this post feeling sad. I actually hope you
feel hopeful! Tons of brilliant engineers are working tirelessly to make your
code run faster.

### Further reading

After publishing this post I came across [Optimizing and Evaluating Transient
Gradual Typing](https://arxiv.org/abs/1902.07808) which adds type checks to
CPython and erases redundant ones, and then realized that this was also cited
in the Brown paper. The whole series of papers is really interesting. And
apparently I was coworkers with Michael Vitousek for over four years. Neat!

<!-- TODO: mention argument clinic typing for FFI boundary?
https://github.com/faster-cpython/ideas/issues/546 -->
<!-- TODO jscomp? https://github.com/tmikov/jscomp -->
<!-- TODO smalls https://github.com/tmikov/smalls -->
<!-- TODO mention Julia and how it handles dispatch -->

<!-- TODO: Address William comments about nominal vs structural typing -->

<!-- TODO Carl Meyer feedback:

I think it is technically true to say that SP compiler compiles a different
language, but relative to some of the other projects on your list, I think you
undersell how compatible that language is, and how smooth the fallback to
dynamic behavior is in almost all cases

ironically the one thing you call out (no __dict__) is really pretty minor: all
you have to do is explicitly request a __dict__ slot and you get one, and it
doesn't impact any other optimizations, it just costs you 64 bits per object

if we wanted to trade off the extra memory for more compatibility (and if
typechecked python didn't already discourage random extra attributes), we could
make the __dict__ slot always there by default, with zero consequence to how SP
works or its performance (other than the memory cost)

the changes in behavior that are more relevant to achieving performance
benefits are checked annotations and stuff related to inheritance (no multiple
data inheritance, can't override a simple attribute with a dynamic property)
-->

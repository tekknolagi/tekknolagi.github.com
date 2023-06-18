---
title: "Compiling typed Python"
layout: post
date: 2023-07-01
---

It's been nine whole years since [PEP 484](https://peps.python.org/pep-0484/)
landed and brought us types from on high. This has made a lot of people very
angry and been widely regarded as a bad move[^adams]. Since then, people on the
internet have been clamoring to find out: [does this mean we can now compile
Python to native code for more
speed](https://discuss.python.org/t/cpython-optimizations-leveraging-type-hints-bachelor-thesis-topic/27264)?

[^adams]: According to Douglas Adams, anyway.

No. But also, kind of, yes. I'll explain. I'll explain using CPython, the
predominant implementation of the Python language. Other runtimes exist, but
they are less common and often have trouble interfacing with C extensions,
which are very prevalent.

## It's not what you think

A lot of people enjoy walking into discussions and saying things like "We have
a 100% mypy typed codebase. I would simply use the types in compilation to
generate better code." That was the original title of this article, even. "*I
would simply use the types in the compiler.*" But it doesn't really work like
that[^simple-annotations]. I'll show a couple examples that demonstrate why.

[^simple-annotations]: I'm not even going to address fancy type-level
    metaprogramming stuff, because I don't think people ever intended to use
    that for performance anyway. We'll just look at very simple bog-standard
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
of `T` or an instance of a subclass of `T`." This is pretty common in
programming languages, and, thanks to [Barbara
Liskov](https://en.wikipedia.org/wiki/Liskov_substitution_principle), makes
semantic sense most of the time. But it doesn't help performance here. The
dispatch for binary operators in Python is famously [not
simple](https://snarky.ca/unravelling-binary-arithmetic-operations-in-python/)
because of subclasses. This means that there is a lot of code to be executed if
`x` and `y` could be any subclasses of `int`. But let's ignore this problem and
pretend that it's not possible to subclass `int`. Problem solved, right? High
performance math?

<!-- TODO add binary op example -->

Unfortunately, no. While we would have fast dispatch on binary operators and
other methods, integers in Python are heap-allocated big integer objects. This
means that every operation on them is a function call to `PyLong_Add` or
similar. While these functions have been optimized for speed over the years,
they are still slower than machine integers. But let's assume that a
sufficiently smart compiler can auto-unbox appropriately-sized big integers
into machine words at the beginning of a function. We can do fast math with
those. If we really want, we can even do fast floating point math, too. Problem
solved? Hopefully?

<!-- TODO add unboxing example -->

Unfortunately, no. Most math kernels are not just using built-in functions and
operators. They call out to external C libraries like NumPy or SciPy, and those
functions expect heap-allocated `PyLongObject`s. The C-API is simply **not
ready** to expose the underlying functions that operate on machine integers,
and it is also **not ready** for tagged pointers. This would be a huge breaking
change in the API and ABI. But okay, let's assume for the sake of blog post
that the compiler team has a magic wand and can do all of this. Then we're set,
right?

<!-- TODO add tagged pointer example -->

Unfortunately, there are still some other loose ends to tie up. While you may
have a nice and neatly typed numeric kernel of Python code, it has to interact
with the outside world. And the outside world is often not so well typed. This
means that at the entry to your typed functions, you have to check the types of
the input objects. Maybe you can engineer a system such that a function can
have multiple entry points---one for untyped calls, one for typed calls, and
maybe even one for unboxed calls---but this hypothetical system's complexity is
growing, and fast.

"But Max," you say, "Python compiler libraries like Numba clearly work just
fine. Just-in-time compilers have been doing this for years. What's the deal?"

Just-in-time compilers can be so effective because can speculate on things that
are not compile-time constants. Then, if the assumption is no longer true, they
can fall back on a (slower) interpreter. This interpreter deoptimization is
part of what makes JITs hard to understand and does not much help with
compiling code ahead-of-time.

And Numba is great! The team put in a lot of hard work and solid engineering
and built a compiler that produces very fast numerical code. But it doesn't
exactly compile *Python*. It compiles a superficially similar language that is
much less dynamic and is focused on numerics. For people who work with data
analytics and machine learning, this is incredible! Unfortunately, it doesn't
generalize to arbitrary Python code.

It does raises an interesting question: what if you intentionally and
explicitly eschew the more dynamic features? Can you get performance in return?
It turns out, yes. Absolutely yes.

## Stronger guarantees

<!-- ok, change the language. cinder? mojo? -->

<!-- typed_python -->

<!-- graal and substratevm -->

<br />
<hr style="width: 100px;" />
<!-- Footnotes -->

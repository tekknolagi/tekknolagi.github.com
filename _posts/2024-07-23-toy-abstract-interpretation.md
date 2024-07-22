---
title: Abstract interpretation in the Toy Optimizer
layout: post
date: 2024-07-23
---

CF Bolz-Tereick wrote some excellent posts in which they [introduce a small IR
and optimizer][toy-optimizer] and [extend it with allocation
removal][toy-allocation-removal]. We also did a live stream together in which
we did [some more heap optimizations][toy-heap].

[toy-optimizer]: https://pypy.org/posts/2022/07/toy-optimizer.html

[toy-allocation-removal]: https://pypy.org/posts/2022/10/toy-optimizer-allocation-removal.html

[toy-heap]: https://www.youtube.com/watch?v=w-UHg0yOPSE

In this blog post, I'm going to write a small abtract interpreter for the Toy
IR and then show how we can use it to do some simple optimizations. It assumes
that you are familiar with the little IR, which I have reproduced unchanged in
[a GitHub Gist][toy-ir].

[toy-ir]: https://gist.github.com/tekknolagi/4425b28d5267e7bae8b0d7ef8fb4a671

Before we begin, I want to note a couple of things:

* The Toy IR is in SSA form, which means that every variable is defined exactly
  once. This means that abstract properties of each variable are easy to track.
* The Toy IR represents a linear trace without control flow, meaning we won't
  talk about meet/join or fixpoints. They only make sense if the IR has a
  notion of conditional branches or back edges (loops).

Alright, let's get started.

## Welcome to abstract interpretation

Abstract interpretation means a couple different things to different people.
There's rigorous mathematical formalism thanks to Patrick and Radhia Cousot,
our favorite power couple, and there's also sketchy hand-wavy stuff like what
will follow in this post. In the end, all people are trying to do is reason
about program behavior without running it.

In particular, abstract interpretation is an *over-approximation* of the
behavior of a program. Correctly implemented abstract interpreters never lie,
but they might be a little bit pessimistic. This is because instead of using
real values and running the program---which would produce a concrete result and
some real-world behavior---we "run" the program with a parallel universe of
*abstract* values.

These abstract values are arranged in a *lattice*, which is a mathematical
structure with some properties but the most important ones are that it has a
top, a bottom, a partial order, a meet operation, and values can only move in
one direction on the lattice.

Using abstract values from a lattice promises two things:

* The analysis will terminate
* The analysis will be correct for *any* run of the program, not just one
  sample run

Let's learn a little about abstract interpretation with an example program and
example abstract domain. Here's the example program:

```
v0 = 1
v1 = 2
v2 = add(v0, v1)
```

And our abstract domain is "is the number positive" (where "positive" means
nonnegative, but I wanted to keep the words distinct):

```
       top
    /       \
positive    negative
    \       /
      bottom
```

The special *top* value means "I don't know" and the special *bottom* value
means "empty set" or "unreachable". The *positive* and *negative* values
represent the sets of all positive and negative numbers, respectively.

We initialize all the variables `v0`, `v1`, and `v2` to *bottom* and then walk
our IR, updating our knowledge as we go.

```
# here
v0:bottom = 1
v1:bottom = 2
v2:bottom = add(v0, v1)
```

In order to do that, we have to have *transfer functions* for each operation.
For constants, the transfer function is easy: determine if the constant is
positive or negative. For other operations, we have to define a function that
takes the abstract values of the operands and returns the abstract value of the
result.

In order to be correct, transfer functions for operations have to be compatible
with the behavior of their corresponding concrete implementations. You can
think of them having an implicit universal quantifier *forall* in front of
them.

Let's step through the constants at least:

```
v0:positive = 1
v1:positive = 2
# here
v2:bottom = add(v0, v1)
```

Now we need to figure out the transfer function for `add`. It's kind of tricky
right now because we haven't specified our abstract domain very well. I keep
saying "numbers", but what kinds of numbers? Integers? Real numbers? Floating
point? Some kind of fixed-width bit vector (`int8`, `uint32`, ...) like an
actual machine "integer"?

For this post, I am going to use the mathematical definition of integer, which
means that the values are not bounded in size and therefore do not overflow.
Actual hardware memory constraints aside, this is kind of like a Python `int`.

So let's look at what happens when we add two abstract numbers:

|              | top | positive | negative | bottom |
|--------------|-----|----------|----------|--------|
| **top**      | top | top      | top      | bottom |
| **positive** | top | positive | top      | bottom |
| **negative** | top | top      | negative | bottom |
| **bottom**   | bottom | bottom | bottom   | bottom |

As an example, let's try to add two numbers `a` and `b`, where `a` is positive
and `b` is negative. We don't know anything about their values other than their
signs. They could be `5` and `-3`, where the result is `2`, or they could be
`1` and `-100`, where the result is `-99`. This is why we can't say anything
about the result of this operation and have to return *top*.

The short of this table is that we only really know the result of an addition
if both operands are positive or both operands are negative. Thankfully, in
this example, both operands are known positive. So we can learn something about
`v2`:

```
v0:positive = 1
v1:positive = 2
v2:positive = add(v0, v1)
# here
```

This may not seem useful in isolation, but analyzing more complex programs even
with this simple domain may be able to remove checks such as `if (v2 < 0) { ... }`.

Other interesting lattices include:

* Constants (where the middle row is pretty wide)
* Range analysis (bounds on min and max of a number)
* Known bits (using a bitvector representation of a number, which bits are
  always 0 or 1)

For the rest of this blog post, we are going to do a very limited version of
"known bits", called *parity*. This analysis only tracks the least significant
bit of a number, which indicates if it is even or odd.

## Parity

The lattice is pretty similar to the positive/negative lattice:

```
     top
  /       \
even      odd
  \       /
    bottom
```

Let's define a data structure to represent this in Python code:

```python
class Parity:
    def __init__(self, name):
        self.name = name

    def __repr__(self):
        return self.name
```

And instantiate the members of the lattice:

```python
TOP = Parity("top")
EVEN = Parity("even")
ODD = Parity("odd")
BOTTOM = Parity("bottom")
```


```
v0 = getarg(0)
v1 = getarg(1)
v2 = lshift(v0, 1)
v3 = lshift(v1, 1)
v4 = add(v2, v3)
v5 = bitand(v4, 1)
v6 = dummy(v5)
```


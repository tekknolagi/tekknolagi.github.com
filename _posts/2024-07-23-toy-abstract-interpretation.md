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

Abstract interpretation is a general framework for efficiently computing
properties that must be true for all possible executions of a program. It's a
widely used approach both in compiler optimizations as well as offline static
analysis for finding bugs. I'm writing this post to pave the way for CF's next
post on proving abstract interpreters correct for range analysis and known bits
analysis inside PyPy.

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
*abstract* values. This abstract run gives us information about all possible
runs of the program.[^logozzo]

[^logozzo]: In the words of abstract interpretation researchers Vincent Laviron
    and Francesco Logozzo in their paper *Refining Abstract
    Interpretation-based Static Analyses with Hints* (APLAS 2009):

    > The three main elements of an abstract interpretation are: (i) the
    > abstract elements ("which properties am I interested in?"); (ii) the
    > abstract transfer functions ("which is the abstract semantics of basic
    > statements?"); and (iii) the abstract operations ("how do I combine the
    > abstract elements?").

    We don't have any of these "abstract operations" in this post because
    there's no control flow but you can read about them elsewhere!

Abstract values always represent sets of concrete values. Instead of literally
storing a set (in the world of integers, for example, it could get pretty
big...there are a lot of integers), we group them into a finite number of named
subsets.[^lattices]

[^lattices]: These abstract values are arranged in a *lattice*, which is a
    mathematical structure with some properties but the most important ones are
    that it has a top, a bottom, a partial order, a meet operation, and values
    can only move in one direction on the lattice.

    Using abstract values from a lattice promises two things:

    * The analysis will terminate
    * The analysis will be correct for *any* run of the program, not just one
      sample run

Let's learn a little about abstract interpretation with an example program and
example abstract domain. Here's the example program:

```python
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

```python
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

```python
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

```python
v0:positive = 1
v1:positive = 2
v2:positive = add(v0, v1)
# here
```

This may not seem useful in isolation, but analyzing more complex programs even
with this simple domain may be able to remove checks such as `if (v2 < 0) { ... }`.

Let's take a look at another example using an sample `absval` (absolute value)
IR operation:

```python
v0 = getarg(0)
v1 = getarg(1)
v2 = absval(v0)
v3 = absval(v1)
v4 = add(v2, v3)
v5 = absval(v4)
```

Even though we have no constant/concrete values, we can still learn something
about the states of values throughout the program. Since we know that `absval`
always returns a positive number, we learn that `v2`, `v3`, and `v4` are all
positive. This means that we can optimize out the `absval` operation on `v5`:

```python
v0:top = getarg(0)
v1:top = getarg(1)
v2:positive = absval(v0)
v3:positive = absval(v1)
v4:positive = add(v2, v3)
v5:positive = v4
```

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
  /     \
even    odd
  \     /
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

Now let's write a forward flow analysis of a basic block using this lattice.
We'll do that by assuming that a method on `Parity` is defined for each IR
operation. For example, `Parity.add`, `Parity.lshift`, etc.

```python
def analyze(block: Block) -> None:
    parity = {v: BOTTOM for v in block}

    def parity_of(value):
        if isinstance(value, Constant):
            return Parity.const(value)
        return parity[value]

    for op in block:
        transfer = getattr(Parity, op.name)
        args = [parity_of(arg.find()) for arg in op.args]
        parity[op] = transfer(*args)
```

For every operation, we compute the abstract value---the parity---of the
arguments and then call the corresponding method on `Parity` to get the
abstract result.

<!-- TODO maybe learn more about different IRs and how they do constants.
apparently pypy/llvm are free-floating; cinder is not -->
We need to special case `Constant`s due to a quirk of how the Toy IR is
constructed: the constants don't appear in the instruction stream and instead
are free-floating.

Let's start by looking at the abstraction function for concrete
values---constants:

```python
class Parity:
    # ...
    @staticmethod
    def const(value):
        if value.value % 2 == 0:
            return EVEN
        else:
            return ODD
```

Seems reasonable enough. Let's pause on operations for a moment and consider an
example program:

```python
v0 = getarg(0)
v1 = getarg(1)
v2 = lshift(v0, 1)
v3 = lshift(v1, 1)
v4 = add(v2, v3)
v5 = dummy(v4)
```

This function (which is admittedly a little contrived) takes two inputs, shifts
them left by one bit, adds the result, and then checks the least significant
bit of the addition result. It then passes that result into a `dummy` function,
which you can think of as "return" or "escape".

To do some abstract interpretation on this program, we'll need to implement the
transfer functions for `lshift` and `add` (`dummy` will just always return
`TOP`). We'll start with `add`. Remember that adding two even numbers returns
an even number, adding two odd numbers returns an even number, and mixing even
and odd returns an odd number.

```python
class Parity:
    # ...
    def add(self, other):
        if self is BOTTOM or other is BOTTOM:
            return BOTTOM
        if self is TOP or other is TOP:
            return TOP
        if self is EVEN and other is EVEN:
            return EVEN
        if self is ODD and other is ODD:
            return EVEN
        return ODD
```

We also need to fill in the other cases where the operands are *top* or
*bottom*. In this case, they are both "contagious"; if either operand is
bottom, the result is as well. If neither is bottom but either operand is top,
the result is as well.

Now let's look at `lshift`. Shifting any number left by a non-zero number of
bits will always result in an even number, but we need to be careful about the
zero case! Shifting by zero doesn't change the number at all. Unfortunately,
since our lattice has no notion of zero, we have to over-approximate here:

```python
class Parity:
    # ...
    def lshift(self, other):
        # self << other
        if other is ODD:
            return EVEN
        return TOP
```

This means that we will miss some opportunities to optimize, but it's a
tradeoff that's just part of the game. (We could also add more elements to our
lattice, but that's a topic for another day.)

Now, if we run our abstract interpretation, we'll collect some interesting
properties about the program. If we temporarily hack on the internals of
`bb_to_str`, we can print out parity information alongside the IR operations:

```python
v0:top = getarg(0)
v1:top = getarg(1)
v2:even = lshift(v0, 1)
v3:even = lshift(v1, 1)
v4:even = add(v2, v3)
v5:top = dummy(v4)
```

This is pretty awesome, because we can see that `v4`, the result of the
addition, is *always* even. Maybe we can do something with that information.

## Optimization

One way that a program might check if a number is odd is by checking the least
significant bit. This is a common pattern in C code, where you might see code
like `y = x & 1`. Let's introduce a `bitand` IR operation that acts like the
`&` operator in C/Python. Here is an example of use of it in our program:

```python
v0 = getarg(0)
v1 = getarg(1)
v2 = lshift(v0, 1)
v3 = lshift(v1, 1)
v4 = add(v2, v3)
v5 = bitand(v4, 1)  # new!
v6 = dummy(v5)
```

We'll hold off on implementing the transfer function for it---that's left as an
exercise for the reader---and instead do something different.

Instead, we'll see if we can optimize operations of the form `bitand(X, 1)`. If
we statically know the parity as a result of abstract interpretation, we can
replace the `bitand` with a constant `0` or `1`.

We'll first modify the `analyze` function (and rename it) to return a new
`Block` containing optimized instructions:

```python
def simplify(block: Block) -> Block:
    parity = {v: BOTTOM for v in block}

    def parity_of(value):
        if isinstance(value, Constant):
            return Parity.const(value)
        return parity[value]

    result = Block()
    for op in block:
        # TODO: Optimize op
        # Emit
        result.append(op)
        # Analyze
        transfer = getattr(Parity, op.name)
        args = [parity_of(arg.find()) for arg in op.args]
        parity[op] = transfer(*args)
    return result
```

We're approaching this the way that PyPy does things under the hood, which is
all in roughly a single pass. It tries to optimize an instruction away, and if
it can't, it copies it into the new block.

Now let's add in the `bitand` optimization. It's mostly some gross-looking
pattern matching that checks if the right hand side of a bitwise `and`
operation is `1` (TODO: the left hand side, too). CF had some neat ideas on how
to make this more ergonomic, which I might save for later.[^match-args]

[^match-args]: Something about `__match_args__` and `@property`... which
    (update) I think I got working in [this
    commit](https://github.com/tekknolagi/toy/commit/1dd285af2a8f64863c350f32925b1ccd0a17c918).

Then, if we know the parity, optimize the `bitand` into a constant.

```python
def simplify(block: Block) -> Block:
    parity = {v: BOTTOM for v in block}

    def parity_of(value):
        if isinstance(value, Constant):
            return Parity.const(value)
        return parity[value]

    result = Block()
    for op in block:
        # Try to simplify
        if isinstance(op, Operation) and op.name == "bitand":
            arg = op.arg(0)
            mask = op.arg(1)
            if isinstance(mask, Constant) and mask.value == 1:
                if parity_of(arg) is EVEN:
                    op.make_equal_to(Constant(0))
                    continue
                elif parity_of(arg) is ODD:
                    op.make_equal_to(Constant(1))
                    continue
        # Emit
        result.append(op)
        # Analyze
        transfer = getattr(Parity, op.name)
        args = [parity_of(arg.find()) for arg in op.args]
        parity[op] = transfer(*args)
    return result
```

Remember: because we use union-find to rewrite instructions in the optimizer
(`make_equal_to`), later uses of the same instruction get the new
optimized version "for free" (`find`).

Let's see how it works on our IR:

```python
v0 = getarg(0)
v1 = getarg(1)
v2 = lshift(v0, 1)
v3 = lshift(v1, 1)
v4 = add(v2, v3)
v6 = dummy(0)
```

Hey, neat! `bitand` disappeared and the argument to `dummy` is now the constant
`0` because we know the lowest bit.

## Wrapping up

Check out the code in the [toy
repository](https://github.com/tekknolagi/toy/tree/90e1a48a8ef7f8ac42b77f83d04278ed3161e248).

Hopefully you have gained a little bit of an intuitive understanding of
abstract interpretation. Last year, being able to write some code made me more
comfortable with the math. Now being more comfortable with the math is helping
me write the code. It's nice upward spiral.

The two abstract domains we used in this post are simple and not very useful in
practice but it's possible to get very far using slightly more complicated
abstract domains. Common domains include: constant propagation, type inference,
range analysis, effect inference, liveness, etc. For example, here is a a
sample lattice for constant propagation:

<figure style="display: block; margin: 0 auto;">
<!--
digraph G {
    rankdir="BT";
    top [shape=Msquare];
    bottom [shape=Msquare];

    bottom -> "-inf";
    bottom -> "-2";
    bottom -> "-1";
    bottom -> 0;
    bottom -> 1;
    bottom -> 2;
    bottom -> "+inf";

    "-inf" -> negative;
    "-2" -> negative;
    "-1" -> negative;
    0 -> top;
    1 -> nonnegative;
    2 -> nonnegative;
    "+inf" -> nonnegative;

    negative -> nonzero;
    nonnegative -> nonzero;
    nonzero->top;

    {rank=same; "-inf"; "-2"; "-1"; 0; 1; 2; "+inf"}
    {rank=same; nonnegative; negative;}
}
-->
    <object class="svg" type="image/svg+xml" data="/assets/img/complex-lattice.svg">
    </object>
</figure>

It has multiple levels to indicate more and less precision. For example, you
might learn that a variable is either `1` or `2` and be able to encode that as
`nonnegative` instead of just going straight to `top`.

Check out some real-world abstract interpretation in open source projects:

* [Known bits in LLVM](https://github.com/llvm/llvm-project/blob/main/llvm/lib/Support/KnownBits.cpp)
* [Constant range in LLVM](https://github.com/llvm/llvm-project/blob/main/llvm/lib/IR/ConstantRange.cpp)
  * But I am told that the ranges don't form a lattice (see [Interval Analysis and Machine Arithmetic: Why Signedness Ignorance Is Bliss](https://dl.acm.org/doi/10.1145/2651360))
* [Tristate numbers for known bits in Linux eBPF](https://github.com/torvalds/linux/blob/master/kernel/bpf/tnum.c)
* [Range analysis in Linux eBPF](https://github.com/torvalds/linux/blob/28bbe4ea686a023929d907cc168430b61094811c/kernel/bpf/verifier.c#L13335)
* [GDB prologue analysis](https://github.com/bminor/binutils-gdb/blob/master/gdb/prologue-value.c)
  of assembly to understand the stack and find frame pointers without using
  DWARF ([some
  docs](https://sourceware.org/gdb/wiki/Internals/Prologue%20Analysis))

If you have some readable examples, please share them so I can add.

## Acknowledgements

Thank you to [CF Bolz-Tereick](https://cfbolz.de/) for the toy optimizer and
helping edit this post!

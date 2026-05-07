---
title: "Partial static single information form"
layout: post
---

In compilers, static single information form (SSI) is a common extension to
static single assignment form (SSA). It was introduced by C. Scott Ananian in
1999 in his [MS thesis](/assets/img/ananian-thesis.pdf) (PDF) [^et-al].

[^et-al]: ...and optimized by [Jeremy Singer in
    2002](/assets/img/singer-ssi.pdf) (PDF), [revisited in
    2009](/assets/img/ssi-revisited.pdf) (PDF), investigated [in 2017 for
    abstract compilation](/assets/img/ssi-abstract-compilation.pdf) (PDF), and
    probably more.

SSI extends your existing SSA intermediate representation by discovering facts
from your existing program and reifying them as path-dependent/flow-sensitive
IR nodes. That might sound complicated, but at least the basic idea is pretty
natural. I talk a little bit about it in [What I talk about when I talk about
IRs](/blog/irs/) but I'll rehash here with some motivating examples. Consider
this admittedly contrived example:

```python
v0: Integer = ...
if v0 > 0:
    # ...
    v1: PositiveInteger = AbsoluteValue v0
    # ...
```

We should be able to learn from the comparison that in some branches in the IR,
`v0` is positive. In that region, we can add a new IR instruction `v2` that
attaches that knowledge right in the type field (yay, sparseness!) and then
rewrite uses of `v0` to now use `v2`.

```python
v0: Integer = ...
if v0 > 0:
    v2: PositiveInteger = RefineType v0, PositiveInteger
    # ...
    v1: PositiveInteger = AbsoluteValue v2
    # ...
```

Because we've done that, our (imaginary) optimization rule that gets rid of
`AbsoluteValue` on known-positive integers can kick in, and we can delete the
invocation of `AbsoluteValue`. Yay, optimization!

But a couple of questions remain, at least for me:

1. Where/when in the compiler pipeline do we insert and remove these type
   refinements?
1. Which regions can we insert these type refinements?
1. Do we need to implement the whole into-SSI and out-of-SSI algorithms from
   all the complicated-looking papers?
1. Do we need to refine after *every* conditional?

We'll go through them.

## When to insert type refinements

* When building SSA: easy, let the SSA-building do the heavy lifting
* When optimizing SSA: need some mechanism to do the operand-use rewrites for you
  * Why not "just" use union-find?

## Which regions

## Complicated-looking papers

## Which conditionals

## In other compilers

* Cinder
* ZJIT
* Graal

## Aside: "separation logic" for e.g. HeapObject upgrade

In ZJIT, we currently insert `RefineType`s opportunistically in "easy" cases
when building our HIR.

For example, if in the bytecode there is a branch that compares some value `x`
with `nil`, it will have two outgoing control-flow edges: one block where `x`
is definitely `nil`, and one block where `x` is definitely *not* `nil`. In each
of these control-flow edges, we can insert corresponding type refinement hints.
That's pretty standard. But we can also do weirder stuff.

CRuby has a notion of heap objects vs immediate objects. Many (most?) objects
are heap objects. However, integer `5`, for example is not allocated on the
heap but instead represented by a [tagged bit pattern](/blog/small-objects/)
that pretends to be an address: the whole value is encoded in the pointer
itself.

We encode this knowledge in the HIR's type system: "heapness" and
"immediateness" each get a bit in the [type lattice](/blog/lattice-bitset/).

On most heap objects, with only a few exceptions, you can write instance
variables (fields, attributes, whatever you want to call them). You can *never*
write an instance variable to an immediate. This means that if we observe the
following pattern in the bytecode:

```
x: BasicObject = ...
setinstancevariable x, :@abc, 1
```

Then after building and emitting HIR for the `setinstancevariable` opcode, we
can upgrade the type of `x` from a `BasicObject` to a `HeapBasicObject`. We can
do this because if it *weren't* a heap-allocated object, we would have left the
compiled code and entered the interpreter.

I keep calling this "separation logic" and I think that's not the right term
but I don't know what is.

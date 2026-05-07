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

SSI extends your existing SSA program by discovering facts from your existing
program and reifying them as path-dependent/flow-sensitive IR nodes. That might
sound complicated, but it's pretty natural. I talk a little bit about it in
[What I talk about when I talk about IRs](/blog/irs/) but I'll rehash here with
some motivating examples. Consider this admittedly contrived example:

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

1. Do we need to implement the whole into-SSI and out-of-SSI algorithms from
   all the complicated-looking papers?
1. Where/when in the compiler pipeline do we insert and remove these type
   refinements?
1. Do we need to refine after *every* conditional?

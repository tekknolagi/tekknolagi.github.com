---
title: "Partial static single information form"
layout: post
---

In compilers, static single information form (SSI) is a common extension to
static single assignment form (SSA). It was introduced by C. Scott Ananian in
1999 in his [MS thesis](/assets/img/ananian-thesis.pdf) (PDF) [^et-al].

[^et-al]: ...and [optimized in 2002](/assets/img/singer-ssi.pdf) (PDF),
    [revisited in 2009](/assets/img/ssi-revisited.pdf) (PDF), [investigated in
    2017 for abstract compilation](/assets/img/ssi-abstract-compilation.pdf)
    (PDF), and probably more. The 2009 paper by Boissinot, Brisk, Darte, and
    Rastello even shows that both Ananian and Singer's papers have bugs, while
    perhaps unintentionally also making an *excellent* pun about the literature
    being "sparse".

SSI extends your existing SSA intermediate representation by discovering facts
from your existing program and reifying them as path-dependent/flow-sensitive
IR nodes. That might sound complicated, but at least the basic idea is pretty
natural. I talk a little bit about it in [What I talk about when I talk about
IRs](/blog/irs/) but I'll rehash here in more depth, starting with some
motivating examples. Consider this admittedly contrived example:

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
    v2: PositiveInteger = RefineType v0, Positive
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

We'll go through them, starting with the compiler pipeline.

## When to insert type refinements

The original SSI paper starts with (TODO: I think?) SSA form and places some
number of new refinement nodes based on conditionals. I have admittedly not
tried very hard, but the into-SSI algorithms look complicated and kind of
heavyweight. As a reward, you get "linear" into-SSI time complexity.

But I am a humble compiler engineer, and I don't have the time to go through
and load all of this into my head. Instead what I have seen done and have been
doing is to take a shortcut: build *partial SSI* during SSA construction.

Most of the time this is from bytecode, but it could also be from some other
non-SSA IR. In any case, this is an excellent shortcut for two reasons:

1. It lets me cleanly separate adding the type refinements (pretty
   straightforward) from the hard part of doing all of the operand rewriting
   and phi placement and marking and all manner of other nonsense. 
2. In addition to separating the concerns, the hard part is *already done* by
   SSA construction. We can actually just skip it! SSA construction handles phi
   placement, operand rewriting, all of it.

This is pretty compelling. We can learn from the bytecode with a very small
amount of marginal new complexity. See [my implementation in
ZJIT][zjit-partial-ssi], for example. All it really does is modify the abstract
interpreter state when building SSA out of `branchnil`, `branchif`, and
`branchunless` bytecode instructions to take into account the new refined
values.

[zjit-partial-ssi]: https://github.com/ruby/ruby/pull/15915/changes#diff-a3cbeb79bf318b2aa8cc979260ba03b0204b436f745dd199a0e0c8ea5c871058

This is fine for branches that are already in the user's source program but
sometimes optimization, especially of dynamic languages, adds new branches that
were not there before. And sometimes these branches get added much later, long
after SSA construction. What then? Can we do something similar and rely on
existing infrastructure?

### During SSA optimization

Implicit in this "can we do it" is the assumption that your IR tracks data
dependencies from use to corresponding def, but *not* from def to uses. Sea of
Nodes, is an IR that tracks both directions

JIT optimization of dynamic language compilers often adds synthetic `Guard`
instructions to the IR that enforce pre-conditions. These guards allow
optimizing happy/fast path cases in JIT code while leaving the interpreter as a
fallback. For example, we might be able to optimize two back-to-back
`setinstancevariable` instructions (a very dynamic operation in the world of
ideas, but fast when concretely implemented using object shapes) from:

```
x = ...
setinstancevariable x, :@a, 1
setinstancevariable x, :@b, 2
# ... use x somewhere ...
```

which is very generic and involves calling into C code that might raise an
exception, to something more like:

```
x = ...
v0 = GuardHeapObject x
v1 = GuardShape v0, 0xcafe
v2 = Const 1
StoreField v1, 0x8, v2
v3 = GuardHeapObject x
v4 = GuardShape v3, 0xcafe
v5 = Const 2
StoreField v4, 0x10, v5
# ... use x somewhere ...
```

which is *much faster* (assuming shape stability at run-time). There's an
irritating problem, though, which is that we have a bunch of duplicate
instructions littered around the IR now because our optimizer worked on each
instruction individually. Kind of a "template optimizer" situation. Now we need
some pass to clean up the detritus.

Global value numbering (GVN) will do a good job of de-duplicating instructions.
It should notice that we already have an instruction that looks like
`GuardHeapObject x` called `v0` and rewrite `v3` into `v3 = v0`. That's great
because we have de-duplicated the guard. GVN may not get everything, though; if
some instructions later use `x`, they will not get rewritten to instead use the
output of these new guard instructions. To do that, we need to add some kind of
`canonicalize` pass or augment GVN with some canonicalization feature. That
canonicalization would handle rewriting operands to use the "latest version" of
some value, so to speak. See the canonicalization section of Chris Fallin's
[excellent aegraphs blog post](https://cfallin.org/blog/2026/04/09/aegraph/)
for more.

Where I'm going with all of this, though, is that you may already have some
dominance-based instruction rewriting mechanism in your compiler, either as
part of GVN or separately! And you can use this to do a very low code
into-partial-SSI in the middle of your optimizer.

```ruby
def canonicalize(bb)
  rewrite_map = {}
  bb.map do |i|
    i.map! do |o|
      rewrite_map[o] || o
    end
    case i.opcode
    when :guardtype
      rewrite_map[i.operands[0]] = i
    end
    i
  end
end
```

* When optimizing SSA: need some mechanism to do the operand-use rewrites for you
  * Why not "just" use union-find?

## Which regions

## Complicated-looking papers

## Which conditionals

## In other compilers

* Cinder
* ZJIT
* Graal
  * https://chrisseaton.com/truffleruby/stamping-out-overflow-checks/
  * `replaceAtUsagesAndDelete` is doing a lot of heavy lifting

## Aside: "separation logic" for e.g. HeapObject upgrade

In ZJIT, we currently insert `RefineType`s opportunistically in "easy" cases
when building our HIR from the interpreter bytecode.

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
"immediateness" each get a bit in the [type lattice](/blog/lattice-bitset/). We
use this in the optimizer to reason about [effects](/blog/compiler-effects/),
among other things.

We can't know a lot of the time what type a thing is, so we pessimistically
type most objects flowing through bytecode as `BasicObject`. This type
encapsulates the entire world of possible values that could go on the stack or
in a local variable.

On most *heap* objects, with only a few exceptions, you can write instance
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

<!--
local reasoning
O'Hearn

Analogue in classical compilers where there is a fast path for parameters not
aliasing

Or that there is some UB-related time travel thing in LLVM+C with null pointer
checks
-->

## Conclusion

Uhh I guess that you don't have to do full SSI and partial SSI is available and
not scary

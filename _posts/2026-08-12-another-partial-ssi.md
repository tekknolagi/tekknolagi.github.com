---
title: "Another partial SSI trick with canonicalize"
layout: post
---

After reading [Chris Fallin's aegraph
post](https://cfallin.org/blog/2026/04/09/aegraph/), new ZJIT contributor dak2
landed a block-local version of the `canonicalize` function in
[#16828](https://github.com/ruby/ruby/pull/16828).

The pseudocode of the block-local canonicalize function looks like this:

```python
for block in function.reverse_post_order():
    rewrite_map = {}
    for insn in block.insns:
        insn.operands.map_in_place(lambda o: rewrite_map.get(o, o))
        if insn.opcode == "GuardType":
            rewrite_map[insn.val] = insn
```

As a refresher, this turns IR like this:

```
v0:Object = ...
v1:Int = GuardType v0, Int
... do something with v1

v2:Int = GuardType v0, Int
... do something with v2
```

into this:

```
v0:Object = ...
v1:Int = GuardType v0, Int
... do something with v1

v2:Int = GuardType v1, Int
... do something with v2
```

Note that the second use of `v0` has been turned into `v1`. This is important
because a later constant-folding pass can observe that the input `v1` of `GuardType
v1, Int` is *already an Int* and can therefore replace all uses of `v2` with
`v1` and delete the guard.

Because in this local version we make a new `rewrite_map` for each block, we
don't carry any rewrites across blocks.

About five days later, dak2 came back with a global version in
[#17013](https://github.com/ruby/ruby/pull/17013)! This PR came with a bunch of
changes in the name of performance---which I appreciate---but I like doing the
silly slow thing first, especially because the PR is so much smaller. We can
always refactor it later to be faster and, in the meantime, use the slow but
maybe-easier-to-verify thing as a correctness oracle. So I did[^much-later] the
silly slow thing in [#17766](https://github.com/ruby/ruby/pull/17766).

[^much-later]: This PR landed much later than dak2's because I wanted to wait
    for the [SSA minimization pass](https://github.com/ruby/ruby/pull/17311) to
    land---more on that another time---so that global canonicalization could do
    more. Otherwise, because we had maximal SSA, we didn't really re-use SSA
    values across blocks.

This copy-happy version of canonicalize looks like:

```python
rewrite_maps = {block: {} for block in blocks}
dominators = compute_dominators()
for block in function.reverse_post_order():
    rewrite_map = rewrite_maps[dominators.idom(block)].clone()
    for insn in block.insns:
        insn.operands.map_in_place(lambda o: rewrite_map.get(o, o))
        if insn.opcode == "GuardType":
            rewrite_map[insn.val] = insn
    rewrite_maps[block] = rewrite_map
```

The core stays the same as the block-local version but now we can cascade
rewrites along the dominator tree. I say that but we're not actually computing
a dominator tree---we're only building a map of `idom` using [the engineered
algorithm](/assets/img/dominators-engineered.pdf) (PDF). The dominator tree
cascades because this RPO-walk+idom-clone approach ends up being equivalent to
actually walking a dominator tree.

The block iteration order is different (RPO vs domtree pre-order) but the only
property we care about maintaining is that we visit dominators before blocks
that get dominated, and that is true in both.

But where was I going with all this?

...

Oh, right. More [partial SSI](/blog/partial-ssi/). In the last post, we
inserted `RefineType` in SSA construction so that we can infer things about the
Ruby type of the conditional. For example:

```
bb0:
  v0: Object = ...
  v1: CBool = Test v0
  v2: Truthy = RefineType v0, Truthy
  v3: Falsy = RefineType v0, Falsy
  CondBranch v1, bb1(v2), bb2(v3)

bb1(v4:Truthy):
  ...

bb2(v5:Falsy):
  ...
```

This is neat when the branch comes from Ruby code but sometimes we synthesize
branches so we can't do this in SSA construction. The general case looks like
this:

```
bb0:
  v0: CBool = ...
  CondBranch v0, bb1, bb2

bb1:
  ...

bb2:
  ...
```

In this more general case, we stil want `bb1` to know that `v0` is
`CBool[true]` in that branch and `bb2` to know that `v0` is `CBool[false]` in
its branch (and in blocks dominated by `bb1` and `bb2`).

Well, this is another thing we can do in `canonicalize`!

All we need to do is at the beginning of each block B:

* Check if we have one incoming control edge E[^block-vs-edge]
* Check if the terminator T for E.block is a conditional branch
* If T.iftrue == B, seed the `rewrite_map` with `T.cond => Const(CBool[true])`
* If T.iffalse == B, seed the `rewrite_map` with `T.cond => Const(CBool[false])`

[^block-vs-edge]: It's possible to have one block A do a conditional branch to
    another block B for both the iftrue case and the iffalse case. This is
    useful if, for example, it is passing different data along the block
    arguments of each edge. For this reason, we check the number of incoming
    edges, not the number of predecessor blocks.

If terminator is CondBranch vNN, bbL, bbM, then we can:
* Seed bbL with a rewrite of vNN to CBool(true)
* Seed bbM with a rewrite of vNN to CBool(false)

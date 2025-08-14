---
title: "Liveness analysis with Datalog"
layout: post
co_authors: Waleed Khan
---

After publishing [Linear scan register allocation on SSA](/blog/linear-scan), I
had a nice call with [Waleet Khan](https://waleedkhan.name) where he showed me
how to Datalog. He thought it might be useful to try implementing liveness
analysis as a Datalog problem.

We started off with the Wimmer2010 CFG example from that post sketching out
manually which variables were live out of each block: R10 out of B1, R12 out of
B2, etc. Then we tried to formulate liveness as a Datalog relation.

Liveness is normally (at least for me) defined in terms of two relations:
live-in and live-out. Live-out is what is needed from the successors of a block
and live-in is the "what is needed" summary for a block. So:

```
live-out(b) = union(live-in(s) for each successor s of b)
live-in(b) = (live-out(b) + used(b)) - defined(b)
```

where each of the component parts of that expression represent sets of
variables:

* *used(b)* is the set of variables referenced as in-operands to instructions in
  a block
* *defined(b)* is the set of variables defined by instructions in a block

We ended up computing the live-in sets for blocks in the register allocator
post but then using the live-out sets instead. So today let's compute some
live-out sets with Datalog!

We'll be using Souffle here because Waleed mentioned it and also I learned a
bit about it in my databases class.

The thing you do first is define your relations. In this case, if we want to
compute liveness information, we have to know information about what a block
uses, defines, and what successors it has.

First, the thing you have to know about Datalog, is that it's kind of like
the opposite of array programming. We're going to express things about sets by
expressing facts about individual items in a set.

For example, we're not going to say "this block B4 uses [R10, R12, R16]". We're
going to say three separate facts: "B4 uses R10", "B4 uses R12", "B4 uses R16".
You can think about it like each relation being a database table where each
parameter is a column name.

Here are the relations for block uses, block defs, and which blocks follow
other blocks:

```
// liveness.dl
.decl block_use(block:symbol, var:symbol)
.decl block_def(block:symbol, var:symbol)
.decl block_succ(succ:symbol, pred:symbol)
```

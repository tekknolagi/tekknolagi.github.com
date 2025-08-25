---
title: "Linear scan with lifetime holes"
layout: post
---

[quality-lsra]: /assets/img/quality-speed-linear-scan-ra-clean.pdf

[lsra-ssa]: /assets/img/wimmer-linear-scan-ssa.pdf

[lsra]: /assets/img/linearscan-ra.pdf

[lsra-context-ssa]: /assets/img/linear-scan-ra-context-ssa.pdf

In my [last post](/blog/lienar-scan/), I explained a bit about how to retrofit
SSA onto the original linear scan algorithm. I went over all of the details for
how to go from low-level IR to register assignments---liveness analysis,
scheduling, building intervals, and the actual linear scan algorithm.

Basically, we made it to 1997 linear scan, with small adaptations for
allocating directly on SSA.

This time, we're going to retrofit *lifetime holes*.

## Lifetime holes

Lifetime holes come into play because a linearized sequence of instructions is
not a great proxy for storing or using metadata about a program originally
stored as a graph.

According to [Linear Scan Register Allocation on SSA Form][lsra-ssa] (PDF,
2010):

> The lifetime interval of a virtual register must cover all parts where this
> register is needed, with lifetime holes in between. Lifetime holes occur
> because the control flow graph is reduced to a list of blocks before register
> allocation. If a register flows into an `else`-block, but not into the
> corresponding `if`-block, the lifetime interval has a hole for the `if`-block.

Lifetime holes come from [Quality and Speed in Linear-scan Register
Allocation][quality-lsra] (PDF, 1998) by Traub, Holloway, and Smith. Figure 1,
though not in SSA form, is a nice diagram for understanding how lifetime holes
may occur. Unfortunately, the paper contains a rather sparse plaintext
description of their algorithm that I did not understand how to apply to my
concrete allocator.

Thankfully, other papers continued this line of research in (at least) 2002,
2005, and 2010. We will piece snippets from those papers together to understand
what's going on.

Let's take a look at the sample IR snippet from Wimmer2010 to illustrate how
lifetime holes form:

```
16: label B1(R10, R11):
18: jmp B2($1, R11)
     # vvvvvvvvvv #
20: label B2(R12, R13)
22: cmp R13, $1
24: branch lessThan B4() else B3()

26: label B3()
28: mul R12, R13 -> R14
30: sub R13, $1 -> R15
32: jump B2(R14, R15)

34: label B4()
     # ^^^^^^^^^^ #
36: add R10, R12 -> R16
38: ret R16
```

Virtual register R12 is not used between position 28 and 34. For this reason,
Wimmer's interval building algorithm assigns it the interval `[[20, 28), [34,
...)]`. Note how the interval has two disjoint ranges with space in the middle.

Our simplified interval building algorithm from last time gave us---in the same
notation---the interval `[[20, ...)]` (well, `[[20, 36)]` in our modified
snippet). This simplified interval only supports one range with no lifetime
holes.

Ideally we would be able to use the physical register assigned to R12 for
another virtual register in this empty slot! For example, maybe R14 or R15,
which have short lifetimes that completely fit into the hole.

Another example is a control-flow diamond. In this example, B1 jumps to either
B3 or B2, which then merge at B4. Virtual register R0 is defined in B1 and only
used in one of the branches, B3. It's also not used in B4---if it were used in
B4, it would be live in both B2 and B3!

<!--
# dot IN.dot -Tsvg -Nfontname=Monospace -Efontname=Monospace > OUT.svg

digraph G {
node [shape=plaintext]
B1 [label=<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0">
<TR><TD PORT="params" BGCOLOR="lightgray">B1()&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">R0 = loadi $123&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="1">blt →B3, →B2&nbsp;</TD></TR>
</TABLE>>];
B1:s -> B3:params:n;
B1:s -> B2:params:n;
B2 [label=<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0">
<TR><TD PORT="params" BGCOLOR="lightgray">B2()&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">R1 = loadi $456&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="1">R2 = add R1, $1&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="2">jump →B4&nbsp;</TD></TR>
</TABLE>>];
B2:s -> B4:params:n;
B3 [label=<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0">
<TR><TD PORT="params" BGCOLOR="lightgray">B3()&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">R3 = mul R0, $2&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="1">jump →B4&nbsp;</TD></TR>
</TABLE>>];
B3:s -> B4:params:n;
B4 [label=<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0">
<TR><TD PORT="params" BGCOLOR="lightgray">B4()&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">ret $5&nbsp;</TD></TR>
</TABLE>>];
}
-->
<figure>
<object class="svg" type="image/svg+xml" data="/assets/img/lsra-diamond-cfg.svg"></object>
</figure>

Once we schedule it, the need for lifetime holes becomes more apparent:

```
0: label B1:
2: R0 = loadi $123
4: blt iftrue: →B3, iffalse: →B2

6: label B2:
8: R1 = loadi $456
10: R2 = add R1, $1
12: jump →B4

14: label B3:
16: R3 = mul R0, $2
18: jump →B4

20: label B4:
22: ret $5
```

Since B2 gets scheduled (in this case, arbitrarily) before B3, there's a gap
where R0---which is completely unused in B2---would otherwise take up space in
our simplified interval form. Let's fix that by adding some lifetime holes.

**Even though** we are adding some gaps between ranges, each interval still
gets assigned *one location for its entire life*. It's just that in the gaps,
we get to put other smaller intervals, like lichen growing between bricks.

To get lifetime holes, we have to modify our interval data structure a bit.

## Finding lifetime holes

Our interval currently only supports a single range:

```ruby
class Interval
  attr_reader :range
  def initialize = raise
  def add_range(from, to) = raise
  def set_from(from) = raise
end
```

We can change this to support multiple ranges by changing *just one character*!!!

```ruby
class Interval
  attr_reader :ranges
  def initialize = raise
  def add_range(from, to) = raise
  def set_from(from) = raise
end
```

Har har. Okay, so we now have an array of `Range` instead of just a single
`Range`. But now we have to implement the methods differently.

We'll start with `initialize`. The start state of an interval is an empty array
of ranges:

```ruby
class Interval
  def initialize
    @ranges = []
  end
end
```

Because we're iterating backwards through the blocks and backwards through
instructions in each block, we'll be starting with instruction 38 and working
our way linearly backwards until 16.

This means that we'll see later uses before earlier uses, and uses before defs.
In order to keep the `@ranges` array in sorted order, we need to add each new
range to the front. This is O(n) in an array, so use a deque or linked list.
(Alternatively, push to the end and then reverse them afterwards.)

<!-- TODO why keep them disjoint? -->

We keep the ranges in sorted order because it makes keeping them disjoint
easier, as we'll see in `add_range` and `set_from`. Let's start with `set_from`
since it's very similar to the previous version:

```ruby
class Interval
  def set_from(from)
    if @ranges.empty?
      # @ranges is empty when we don't have a use of the vreg
      @ranges << Range.new(from, from)
    else
      @ranges[0] = Range.new(from, @ranges[0].end)
    end
    assert_sorted_and_disjoint
  end
end
```

`add_range` has a couple more cases, but we'll go through them step by step.
First, a quick check that the range is the right way 'round:

```ruby
class Interval
  def add_range(from, to)
    if to <= from
      raise ArgumentError, "Invalid range: #{from} to #{to}"
    end
    # ...
  end
end
```

Then we have a straightforward case: if we don't have any ranges yet, add a
brand new one:

```ruby
class Interval
  def add_range(from, to)
    # ...
    if @ranges.empty?
      @ranges << Range.new(from, to)
      return
    end
    # ...
  end
end
```

But if we do have ranges, this new range might be totally subsumed by the
existing first range. This happens if a virtual register is live for the
entirety of a block and also used inside that block. The uses that cause an
`add_range` don't add any new information:

```ruby
class Interval
  def add_range(from, to)
    # ...
    if @ranges.first.cover?(from..to)
      assert_sorted_and_disjoint
      return
    end
    # ...
  end
end
```

Another case is that the new range has a partial overlap with the existing
first range. This happens when we're adding ranges for all of the live-out
virtual registers; the range for the predecessor block (say `[4, 8]`) will abut
the range for the successor block (say `[8, 12]`). We merge these ranges into
one big range (say, `[4, 12]`):

```ruby
class Interval
  def add_range(from, to)
    # ...
    if @ranges.first.cover?(to)
      @ranges[0] = Range.new(from, @ranges.first.end)
      assert_sorted_and_disjoint
      return
    end
    # ...
  end
end
```

The last case is the case that gives us lifetime holes and happens when the new
range is already completely disjoint from the existing first range. That is
also a straightforward case: put the new range in at the start of the list.

```ruby
class Interval
  def add_range(from, to)
    # ...
    # TODO(max): Use a linked list or deque or something to avoid O(n) insertions
    @ranges.insert(0, Range.new(from, to))
    assert_sorted_and_disjoint
    # ...
  end
end
```

This is all fine and good. I added this to the register allocator to test out
the lifetime hole finding but kept the rest of the same (changed the APIs
slightly so the interval could pretend it was still one big range). The tests
passed. Neat!

I also verified that the lifetime holes were what we expected. This means our
`build_intervals` function works unmodified with the new `Interval`
implementation. That makes sense, given that we copied the implementation off
of Wimmer2010, which can deal with lifetime holes.

Now we would like to use this new information in the register allocator.

## Modified linear scan

It took a little bit of untangling, but the required modifications to support
lifetime holes in the register assignment phase are not too invasive. To get an
idea of the difference, I took the original [Poletto1999][lsra] (PDF) algorithm
and rewrote it in the style of the [Mössenböck2002][lsra-context-ssa] (PDF)
algorithm.

For example, here is Poletto1999:

```
LinearScanRegisterAllocation
active ← {}
foreach live interval i, in order of increasing start point
  ExpireOldIntervals(i)
  if length(active) = R then
    SpillAtInterval(i)
  else
    register[i] ← a register removed from pool of free registers
    add i to active, sorted by increasing end point

ExpireOldIntervals(i)
foreach interval j in active, in order of increasing end point
  if endpoint[j] ≥ startpoint[i] then
    return
  remove j from active
  add register[j] to pool of free registers

SpillAtInterval(i)
spill ← last interval in active
if endpoint[spill] > endpoint[i] then
  register[i] ← register[spill]
  location[spill] ← new stack location
  remove spill from active
  add i to active, sorted by increasing end point
else
  location[i] ← new stack location
```

And here it is again, reformatted a bit. The implicit `unhandled` and `handled`
sets that don't get names in Poletto1999 now get names. `ExpireOldIntervals` is
inlined and `SpillAtInterval` gets a new name:

```
LINEARSCAN()
unhandled ← all intervals in increasing order of their start points
active ← {}; handled ← {}
free ← set of available registers
while unhandled ≠ {} do
  cur ← pick and remove the first interval from unhandled
  //----- check for active intervals that expired
  for each interval i in active do
    if i ends before cur.beg then
      move i to handled and add i.reg to free

  //----- collect available registers in f
  f ← free

  //----- select a register from f
  if f = {} then
    ASSIGNMEMLOC(cur) // see below
  else
    cur.reg ← any register in f
    free ← free – {cur.reg}
    move cur to active

ASSIGNMEMLOC(cur: Interval)
spill ← last interval in active
if spill.end > cur.end then
  cur.reg ← spill.reg
  spill.location ← new stack location 
  move spill from active to handled
  move cur to active
else
  cur.location ← new stack location 
```

Now we can pick out all of the bits of Mössenböck2002 that look like they are
responsible for dealing with lifetime holes.

For example, the algorithm now has a fourth set, `inactive`. This set holds
intervals that have holes that contain the current interval's start position.
These intervals are assigned registers that are potential candidates for the
current interval to live (more on this in a sec).

I say potential candidates because in order for them to be a home for the
current interval, an inactive interval has to be completely disjoint from the
current interval. If they overlap at all---in any of their ranges---then we
would be trying to put two virtual registers into one physical register at the
same program point. That's a bad compile.

We have to do a little extra bookkeeping in `ASSIGNMEMLOC` because now one
physical register can be assigned to more than one interval that is still in
the middle of being processed (active and inactive sets). If we choose to
spill, we have to make sure that all conflicting uses of the register
(intervals that overlap with the current interval) get reassigned locations.

```
LINEARSCAN()
unhandled ← all intervals in increasing order of their start points
active ← {}; handled ← {}
inactive ← {}
free ← set of available registers
while unhandled ≠ {} do
  cur ← pick and remove the first interval from unhandled
  //----- check for active intervals that expired
  for each interval i in active do
    if i ends before cur.beg then
      move i to handled and add i.reg to free
    else if i does not overlap cur.beg then
      move i to inactive and add i.reg to free
  //----- check for inactive intervals that expired or become reactivated
  for each interval i in inactive do
    if i ends before cur.beg then
      move i to handled
    else if i overlaps cur.beg then
      move i to active and remove i.reg from free

  //----- collect available registers in f
  f ← free
  for each interval i in inactive that overlaps cur do f ← f – {i.reg}

  //----- select a register from f
  if f = {} then
    ASSIGNMEMLOC(cur) // see below
  else
    cur.reg ← any register in f
    free ← free – {cur.reg}
    move cur to active

ASSIGNMEMLOC(cur: Interval)
spill ← heuristic: pick some interval from active or inactive
if spill.end > cur.end then
  r = spill.reg
  conflicting = set of active or inactive intervals with register r that
    overlap with cur
  move all intervals in conflicting to handled
  assign memory locations to them
  cur.reg ← r
  move cur to active
else
  cur.location ← new stack location 
```

Note that this begins to depart from strictly linear linear scan: the
`inactive` set is bounded not by the number of physical registers but instead
by the number of virtual registers. Mössenböck2002 notes that the size of the
set is generally very small, though, so "linear in practice".

I left out the parts about register weights that are heuristics to improve
register allocation. They are not core to supporting lifetime holes. You can
add them back in if you like.

Here is a text diff to make it clear what changed:

```diff
diff --git a/tmp/lsra b/tmp/lsra-holes
index e9de35b..de79a63 100644
--- a/tmp/lsra
+++ b/tmp/lsra-holes
@@ -1,6 +1,7 @@
 LINEARSCAN()
 unhandled ← all intervals in increasing order of their start points
 active ← {}; handled ← {}
+inactive ← {}
 free ← set of available registers
 while unhandled ≠ {} do
   cur ← pick and remove the first interval from unhandled
@@ -8,9 +9,18 @@ while unhandled ≠ {} do
   for each interval i in active do
     if i ends before cur.beg then
       move i to handled and add i.reg to free
+    else if i does not overlap cur.beg then
+      move i to inactive and add i.reg to free
+  //----- check for inactive intervals that expired or become reactivated
+  for each interval i in inactive do
+    if i ends before cur.beg then
+      move i to handled
+    else if i overlaps cur.beg then
+      move i to active and remove i.reg from free

   //----- collect available registers in f
   f ← free
+  for each interval i in inactive that overlaps cur do f ← f – {i.reg}

   //----- select a register from f
   if f = {} then
@@ -23,10 +33,10 @@ while unhandled ≠ {} do
 ASSIGNMEMLOC(cur: Interval)
-spill ← last interval in active
+spill ← heuristic: pick some interval from active or inactive
 if spill.end > cur.end then
-  cur.reg ← spill.reg
-  spill.location ← new stack location
-  move spill from active to handled
+  r = spill.reg
+  conflicting = set of active or inactive intervals with register r that
+    overlap with cur
+  move all intervals in conflicting to handled
+  assign memory locations to them
+  cur.reg ← r
   move cur to active
 else
   cur.location ← new stack location
```

This reformatting and diffing made it much easier for me to reason about what
specifically had to be changed.

There's just one thing left after register assignment: resolution and SSA
deconstruction.

## Resolution and SSA destruction

I'm pretty sure we can actually just keep the resolution the same. In our
`resolve` function, we are only making sure that the block arguments get
parallel-moved into the block parameters. That hasn't changed.

Wimmer2010 says:

> Linear scan register allocation with splitting of lifetime intervals requires
> a resolution phase after the actual allocation. Because the control flow
> graph is reduced to a list of blocks, control flow is possible between blocks
> that are not adjacent in the list. When the location of an interval is
> different at the end of the predecessor and at the start of the successor, a
> move instruction must be inserted to resolve the conflict.

That's great news for us: we don't do splitting. An interval, though it has
lifetime holes, still only ever has one location for its entire life. So once
an interval begins, we don't need to think about moving its contents.

So I was actually overly conservative in the previous post, which I have
amended!

## Fixed intervals and register constraints?

Mössenböck2002 also tackles register constraints with this notion of "fixed
intervals"---intervals that have been pre-allocated physical registers.

Since I eventually want to use "register hinting" from Wimmer2005 and
Wimmer2010, I'm going to ignore the fixed interval part of Mössenböck2002 for
now. It seems like they work nicely together.

## Wrapping up

We added lifetime holes to our register allocator without too much effort. This
should get us some better allocation for short-lived virtual registers.

Maybe next time we will add *interval splitting*, which will help us a) address
ABI constraints more cleanly in function calls and b) remove the dependence on
reserving a scratch register.

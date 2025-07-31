---
title: "Linear scan register allocation on SSA"
layout: post
---

How do JIT compilers do register allocation? Well, "everyone knows" that
"every JIT does its own variant of linear scan". This bothered me for some time
because I've worked on a couple of JITs and still didn't understand the backend
bits.

I started reading [Linear Scan Register Allocation on SSA
Form](/assets/img/wimmer-linear-scan-ssa.pdf) (PDF, 2010) by Wimmer and Franz
after writing [A catalog of ways to generate SSA](/blog/ssa/). Reading alone
didn't make a ton of sense---I ended up with a lot of very frustrated margin
notes. I started trying to implement it alongside the paper. As it turns out,
though, there is a rich history of papers in this area that it leans on really
heavily. I needed to follow the chain of references!

I didn't realize that there were more than one or two papers on linear scan. So
this post will serve as a bit of a survey or a history of linear scan---as best
as I can figure it out, anyway. If you were in or near the room where it
happened, please feel free to reach out and correct some parts.

## Register allocation

The fundamental problem in register allocation is to take an IR that uses a
virtual registers (as many as you like) and rewrite it to use a finite amount
of physical registers and stack space.

People use register allocators like they use garbage collectors: it's an
abstraction that can manage your resources for you, maybe with some cost. When
writing the back-end of a compiler, it's probably much easier to have a
separate register-allocator-in-a-box than manually managing variable lifetimes
while also considering all of your different target architectures.

There are a couple different approaches to register allocation, but in this
post we'll focus on *linear scan of SSA*. Throughout this post, we'll use an
example SSA code snippet from Wimmer2010, adapted from phi-SSA to
block-argument-SSA. Wimmer2010's code snippet is between the arrows and we add some
filler (as alluded to in the paper):

```
label B1(R10, R11):
jmp B2(1, R11)
 # vvvvvvvvvv #
label B2(R12, R13)
cmp R13, 1
branch lessThan B4()

label B3()
mul R12, R13 -> R14
sub R13, 1 -> R15
jump B2(R14, R15)

label B4()
 # ^^^^^^^^^^ #
add R10, R12 -> R16
ret R16
```

Virtual registers start with R and are defined either with an arrow or by a
block parameter.

Because it takes a moment to untangle the unfamiliar syntax and draw the
control-flow graph by hand, I've also provided the same code in graphical form.
Block names (and block parameters) are shaded with grey.

<!--
digraph G {
node [shape=plaintext]
B1 [label=<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0">
<TR><TD PORT="params" BGCOLOR="lightgray">B1(V10, V11)&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">jump →B2($1, V11)&nbsp;</TD></TR>
</TABLE>>];
B1:0 -> B2:params;
B2 [label=<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0">
<TR><TD PORT="params" BGCOLOR="lightgray">B2(V12, V13)&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">cmp V13, $1&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="1">blt →B4, →B3&nbsp;</TD></TR>
</TABLE>>];
B2:1 -> B4:params;
B2:1 -> B3:params;
B3 [label=<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0">
<TR><TD PORT="params" BGCOLOR="lightgray">B3()&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">V14 = mul V12, V13&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="1">V15 = sub V13, $1&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="2">jump →B2(V14, V15)&nbsp;</TD></TR>
</TABLE>>];
B3:2 -> B2:params;
B4 [label=<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0">
<TR><TD PORT="params" BGCOLOR="lightgray">B4()&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">V16 = add V10, V12&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="1">ret V16&nbsp;</TD></TR>
</TABLE>>];
}
-->
<figure>
<object class="svg" type="image/svg+xml" data="/assets/img/wimmer-lsra-cfg.svg"></object>
<figcaption>
blah TODO
</figcaption>
</figure>

Our goal for the post is to analyze this CFG, assign physical locations
(registers or stack slots) to each virtual register, and then rewrite the code
appropriately.

For now, let's rewind the clock and look at how linear scan came about.

## In the beginning

Linear scan register allocation (LSRA) has been around for awhile. It's neat
because it does the actual register assignment part of register allocation in
one pass over your low-level IR. (We'll talk more about what that means in a
minute.)

It first appeared in the literature in [tcc: A System for Fast, Flexible, and
High-level Dynamic Code Generation](/assets/img/tcc-linearscan-ra.pdf) (PDF,
1997) by Poletto, Engler, and Kaashoek. (Until writing this post, I had never
seen this paper. It was only on a re-read of the 1999 paper (below) that I
noticed it.) In this paper, they mostly describe a staged variant of C called
'C (TickC), for which a fast register allocator is quite useful.

Then came a paper called [Quality and Speed in Linear-scan Register
Allocation](/assets/img/quality-speed-linear-scan-ra.pdf) (PDF, 1998) by Traub,
Holloway, and Smith. It adds some optimizations (lifetime holes, binpacking) to
the algorithm presented in the 1997 paper.

The first paper I read, and I think the paper everyone refers to when they talk
about linear scan, is [Linear Scan Register
Allocation](/assets/img/linearscan-ra.pdf) (PDF, 1999) by Poletto and Sarkar.
In this paper, they give a fast alternative to graph coloring register
allocation, especially motivated by just-in-time compilers. In retrospect, it
seems to be a bit of a rehash of the previous two papers.

Linear scan (1997, 1999) operates on *live ranges* instead of virtual
registers. A live range is a pair of integers [start, end) (end is exclusive) that
begins when the register is defined and ends when it is last used. In
non-SSA-land, these live ranges are different from the virtual registers: they
represent some kind of lifetimes of each *version* of a virtual register. For
an example, consider the following code snippet:

```
...      -> a
add 1, a -> b
add 1, b -> c
add 1, c -> a
add 1, a -> d
```

There are two definitions of `a` and they each live for different amounts of
time:

```
                  a  b  c  a  d
...      -> a     |                <- the first a
add 1, a -> b     v  |
add 1, b -> c        v  |
add 1, c -> a           v  |       <- the second a
add 1, a -> d              v  |
```

In fact, the ranges are completely disjoint. It wouldn't make sense for the
register allocator to consider variables, because there's no reason the two
`a`s should necessarily live in the same physical register.

In SSA land, it's a little different: since each virtual registers only has one
definition (by, uh, definition), live ranges are an exact 1:1 mapping with
virtual registers. **We'll focus on SSA for the remainder of the post because
this is what I am currently interested in.** The research community seems to
have decided that allocating directly on SSA gives more information to the
register allocator.

Linear scan starts at the point in your compiler process where you already know
these live ranges---that you have already done some kind of analysis to build a
mapping.

Part of this analysis is called *liveness analysis*.

## Liveness analysis

The result of liveness analysis is a mapping of `BasicBlock ->
Set[Instruction]` that tells you which virtual registers (remember, since we're
in SSA, instruction==vreg) are alive (used later) at the beginning of the basic
block. This is called a *live-in* set. For example:

```
B0:
... -> R12
... -> R13
jmp B1

B1:
mul R12, R13 -> R14
sub R13, 1 -> R15
jmp B2

B2:
add R14, R15 -> R16
ret R16
```

We compute liveness by working backwards: a variable is *live* from the moment
it is backwardly-first used until its definition.

In this case, at the end of B2, nothing is live. If we step backwards to the
`ret`, we see a use: R16 becomes live. If we step once more, we see its
definition---R16 no longer live---but now we see a use of R14 and R15, which
become live. This leaves us with R14 and R15 being *live-in* to B2.

This live-in set becomes B1's *live-out* set. We continue in B1. We could
continue backwards linearly through the blocks. In fact, I encourage you to do
it as an exercise. You should have a (potentially emtpy) set of registers per
basic block.

It gets more interesting, though, when we have branches: what does it mean when
two blocks' live-in results merge into their shared predecessor? If we have two
blocks A and B that are successors of a block C, the live-in sets get
*unioned* together.

<!--
digraph G {
  node [shape=square];
  C -> A;
  C -> B;
}
-->
<figure>
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="98pt" height="116pt" viewBox="0.00 0.00 98.00 116.00">
<g id="graph0" class="graph" transform="scale(1 1) rotate(0) translate(4 112)">
<title>G</title>
<polygon fill="white" stroke="none" points="-4,4 -4,-112 94,-112 94,4 -4,4"/>
<!-- C -->
<g id="node1" class="node">
<title>C</title>
<polygon fill="none" stroke="black" points="63,-108 27,-108 27,-72 63,-72 63,-108"/>
<text text-anchor="middle" x="45" y="-85.8" font-family="Times,serif" font-size="14.00">C</text>
</g>
<!-- A -->
<g id="node2" class="node">
<title>A</title>
<polygon fill="none" stroke="black" points="36,-36 0,-36 0,0 36,0 36,-36"/>
<text text-anchor="middle" x="18" y="-13.8" font-family="Times,serif" font-size="14.00">A</text>
</g>
<!-- C&#45;&gt;A -->
<g id="edge1" class="edge">
<title>C-&gt;A</title>
<path fill="none" stroke="black" d="M38.33,-71.7C35.42,-64.15 31.93,-55.12 28.68,-46.68"/>
<polygon fill="black" stroke="black" points="32.01,-45.59 25.14,-37.52 25.48,-48.11 32.01,-45.59"/>
</g>
<!-- B -->
<g id="node3" class="node">
<title>B</title>
<polygon fill="none" stroke="black" points="90,-36 54,-36 54,0 90,0 90,-36"/>
<text text-anchor="middle" x="72" y="-13.8" font-family="Times,serif" font-size="14.00">B</text>
</g>
<!-- C&#45;&gt;B -->
<g id="edge2" class="edge">
<title>C-&gt;B</title>
<path fill="none" stroke="black" d="M51.67,-71.7C54.58,-64.15 58.07,-55.12 61.32,-46.68"/>
<polygon fill="black" stroke="black" points="64.52,-48.11 64.86,-37.52 57.99,-45.59 64.52,-48.11"/>
</g>
</g>
</svg>
<figcaption>Working backwards from each of A and B, TODO</figcaption>
</figure>

That is, if there were some register R0 live-in to B and some register R1
live-in to A, both R0 and R1 would be live-out of C. They may also be live-in
to C, but that entirely depends on the contents of C.

Since the total number of virtual registers is nonnegative and is finite for a
given program, it seems like a good lattice for an *abstract interpreter*.
That's right, we're doing AI.

In this liveness analysis, we'll:

1. compute a summary of what virtual registers each basic block needs to be
   alive (gen set) and what variables it defines (kill set)
1. initialize all live-in sets to 0
1. do an iterative dataflow analysis over the blocks until the live-in sets
   converge

We store gen, kill, and live-in sets as bitsets, using some APIs conveniently
available on Ruby's Integer class.

Like most abstract interpretations, it doesn't matter what order we iterate
over the collection of basic blocks for correctness, but it *does* matter for
performance. In this case, iterating backwards (`post_order`) converges much
faster than forwards (`reverse_post_order`):

```ruby
class Function
  def compute_initial_liveness_sets order
    # Map of Block -> what variables it alone needs to be live-in
    gen = Hash.new 0
    # Map of Block -> what variables it alone defines
    kill = Hash.new 0
    order.each do |block|
      block.instructions.reverse_each do |insn|
        out = insn.out&.as_vreg
        if out
          kill[block] |= (1 << out.num)
        end
        insn.vreg_ins.each do |vreg|
          gen[block] |= (1 << vreg.num)
        end
      end
      block.parameters.each do |param|
        kill[block] |= (1 << param.num)
      end
    end
    [gen, kill]
  end

  def analyze_liveness
    order = post_order
    gen, kill = compute_initial_liveness_sets(order)
    # Map from Block -> what variables are live-in
    live_in = Hash.new 0
    changed = true
    while changed
      changed = false
      for block in order
        # Union-ing all the successors' live-in sets gives us this block's
        # live-out, which is a good starting point for computing the live-in
        block_live = block.successors.map { |succ| live_in[succ] }.reduce(0, :|)
        block_live |= gen[block]
        block_live &= ~kill[block]
        if live_in[block] != block_live
          changed = true
          live_in[block] = block_live
        end
      end
    end
    live_in
  end
end
```

We could also use a worklist here, and it would be faster, but eh. Repeatedly
iterating over all blocks is fine for now.

The Wimmer2010 paper skips this liveness analysis entirely by assuming some
computed information about your CFG: where loops start and end. It also
requires all loop blocks be contiguous. Then it makes variables defined before
a loop and used at any point inside the loop live *for the whole loop*. By
having this information available, it folds the liveness analysis into the live
range building, which we'll do in a moment.

That sounded complicated and finicky. Maybe it is, maybe it isn't. So I went
with a dataflow liveness analysis instead. If it turns out to be the slow part,
maybe it will matter enough to learn about this loop tagging method.

For now, we will pick a *schedule* for the control-flow graph.

## Scheduling

In order to build live ranges, you have to have some kind of numbering system
for your instructions, otherwise a live range's start and end are meaningless.
We can write a function that fixes a particular block order (in this case,
reverse post-order) and then assigns each block and instruction a number in a
linear sequence. You can think of this as flattening or projecting the graph:

```ruby
class Function
  def number_instructions!
    @block_order = rpo
    number = 16  # just so we match the Wimmer2010 paper
    @block_order.each do |blk|
      blk.number = number
      number += 2
      blk.instructions.each do |insn|
        insn.number = number
        number += 2
      end
      blk.to = number
    end
  end
end
```

A couple interesting things to note:

* We number blocks because we use block starts as the start index for all of
  that block's parameters
* We start numbering at 16 just so we can eyeball things and make sure they
  line up with the Wimmer2010 paper
* We only give out even numbers because... why? TODO
  * Also note that some implementations online seem to do like 1.1 and 2.1
  (floats???)

Even though we have extra instructions, it looks very similar to the example in
the Wimmer2010 paper.

```
16: label B1(R10, R11):
18: jmp B2(1, R11)
     # vvvvvvvvvv #
20: label B2(R12, R13)
22: cmp R13, 1
24: branch lessThan B4()

26: label B3()
28: mul R12, R13 -> R14
30: sub R13, 1 -> R15
32: jump B2(R14, R15)

34: label B4()
     # ^^^^^^^^^^ #
36: add R10, R12 -> R16
38: ret R16
```

Since we're not going to be messing with the order of the instructions within a
block anymore, all we have to do going forward is make sure that we iterate
through the blocks in `@block_order`.

Finally, we have all that we need to compute live ranges.

## Live ranges

We'll more or less copy the algorithm to compute live ranges from the
Wimmer2010 paper. We'll have two main differences:

* We're going to compute live ranges, not live intervals (as they do in the
  paper)
* We're going to use our dataflow liveness analysis, not the loop header thing

I know I said we were going to be computing live ranges. So why am I presenting
you with a function called `build_intervals`? That's because somewhere (TODO where?) in
the history of linear scan, people moved from having a single range for a
particular virtual register to having *multiple* disjoint ranges. This
collection of multiple ranges is called an *interval* and it exists to free up
registers in the context of branches.

For example, in the our IR snippet (above), R12 is defined in B2 as a block
parameter, used in B3, and then not used again until some indetermine point in
B4. (Our example uses it immediately in an add instruction to keep things
short, but pretend the second use is some time away.)

The Wimmer2010 paper creates a *lifetime hole* between 28 and 34, meaning that the
interval for R12 (called i12) is `[[20, 28), [34, ...)]`. Interval holes are
not strictly necessary---they exist to generate better code. So for this post,
we're going to start simple and assume 1 interval == 1 range. We may come back
later and add additional ranges, but that will require some fixes to our later
implementation. We'll note where we think those fixes should happen.

Anyay, here is the mostly-copied annotated implementation of BuildIntervals
from the Wimmer2010 paper:

```ruby
class Function
  def build_intervals live_in
    intervals = Hash.new { |hash, key| hash[key] = Interval.new }
    @block_order.each do |block|
      # live = union of successor.liveIn for each successor of b
      # this is the *live out* of the current block since we're going to be
      # iterating backwards over instructions
      live = block.successors.map { |succ| live_in[succ] }.reduce(0, :|)
      # for each phi function phi of successors of b do
      #   live.add(phi.inputOf(b))
      live |= block.out_vregs.map { |vreg| 1 << vreg.num }.reduce(0, :|)
      each_bit(live) do |idx|
        opd = vreg idx
        intervals[opd].add_range(block.from, block.to)
      end
      block.instructions.reverse.each do |insn|
        out = insn.out&.as_vreg
        if out
          # for each output operand opd of op do
          #   intervals[opd].setFrom(op.id)
          intervals[out].set_from(insn.number)
        end
        # for each input operand opd of op do
        #   intervals[opd].addRange(b.from, op.id)
        insn.vreg_ins.each do |opd|
          intervals[opd].add_range(block.from, insn.number)
        end
      end
    end
    intervals.default_proc = nil
    intervals.freeze
  end
end
```

Another difference is that since we're using block parameters, we don't really
have this `phi.inputOf` thing. That's just the block argument.

The last difference is that since we're skipping the loop liveness hack, we
don't modify a block's `live` set as we iterate through instructions.

I know we said we're building live ranges, so our `Interval` class only has
one `Range` on it. This is Ruby's built-in range, but it's really just being
used as a tuple of integers here.

```ruby
class Interval
  attr_reader :range

  def add_range(from, to)
    if to <= from
      raise ArgumentError, "Invalid range: #{from} to #{to}"
    end
    if !@range
      @range = Range.new(from, to)
      return
    end
    @range = Range.new([@range.begin, from].min, [@range.end, to].max)
  end

  def set_from(from)
    @range = if @range
      Range.new(from, @range.end)
    else
      # This happens when we don't have a use of the vreg
      # If we don't have a use, the live range is very short
      Range.new(from, from)
    end
  end

  def ==(other)
    other.is_a?(Interval) && @range == other.range
  end
end
```

## Linear scan

Because we have faithfully kept 1 interval == 1 range, we can re-use the linear
scan algorithm from PolettoSarkar1999 (which looks, at a glance, to be the same
as 1997).

```ruby
class Function
  def ye_olde_linear_scan intervals, num_registers
    if num_registers <= 0
      raise ArgumentError, "Number of registers must be positive"
    end
    free_registers = Set.new 0...num_registers
    active = []  # Active intervals, sorted by increasing end point
    assignment = {}  # Map from Interval to PReg|StackSlot
    num_stack_slots = 0
    # Iterate through intervals in order of increasing start point
    sorted_intervals = intervals.sort_by { |_, interval| interval.range.begin }
    sorted_intervals.each do |_vreg, interval|
      # expire_old_intervals(interval)
      active.select! do |active_interval|
        if active_interval.range.end >= interval.range.begin
          true
        else
          operand = assignment.fetch(active_interval)
          raise "Should be assigned a register" unless operand.is_a?(PReg)
          free_registers.add(operand.name)
          false
        end
      end
      if active.length == num_registers
        # spill_at_interval(interval)
        # Pick an interval to spill. Picking the longest-lived active one is
        # a heuristic from the original linear scan paper.
        spill = active.last
        # In either case, we need to allocate a slot on the stack.
        slot = StackSlot.new(num_stack_slots)
        num_stack_slots += 1
        if spill.range.end > interval.range.end
          # The last active interval ends further away than the current interval; spill it.
          assignment[interval] = assignment[spill]
          if !assignment[interval].is_a?(PReg)
            raise "Should be assigned a register"
          end
          assignment[spill] = slot
          active.pop  # We know spill is the last one
          # Insert interval into already-sorted active
          insert_idx = active.bsearch_index { |i| i.range.end >= interval.range.end } || active.length
          active.insert(insert_idx, interval)
        else
          # The current interval ends further away than the last active
          # interval; spill it.
          assignment[interval] = slot
        end
      else
        reg = free_registers.min
        free_registers.delete(reg)
        assignment[interval] = PReg.new(reg)
        # Insert interval into already-sorted active
        insert_idx = active.bsearch_index { |i| i.range.end >= interval.range.end } || active.length
        active.insert(insert_idx, interval)
      end
    end
    [assignment, num_stack_slots]
  end
end
```

## Resolving SSA

## Instruction selection

## ............







The liveness analysis tells you which basic
blocks need which virtual registers to be alive on entry. This is a
*graph-land* notion: it operates on your control-flow graph which has not yet
been assigned an order.

Consider the following code snippet:

```
B0:
... -> R12
... -> R13
jmp B1

B1:
mul R12, R13 -> R14
sub R13, 1 -> R15
jmp B2

B2:
add R14, R15 -> R16
ret R16
```

It looks scheduled, but really B1 and B2 could be swapped (for example) and the
code would work just fine. No instruction has *actually* been assigned an
address yet.

Consider the following sketchy annotation of live ranges in a made-up
assembly-like language with virtual registers:

```
                       R12 R13 R14 R15 R16
R12 = ...              |
R13 = ...              |   |
mul R12, R13 -> R14    v   |   |
sub R13, 1 -> R15          |   |   |
add R14, R15 -> R16        v   v   v   |
print R16                              v
```

TODO insert a diagram

The interval construction tells you where a virtual register is first defined
and where it is last used.


After liveness analysis, you need to build intervals.

Implicit here is that you have already *scheduled* your instructions. So you
need to do that too.

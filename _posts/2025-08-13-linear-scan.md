---
title: "Linear scan register allocation on SSA"
layout: post
description: "A linear scan through the history of... linear scan register allocation."
---

*Much of the code and education that resulted in this post happened with [Aaron
Patterson](https://tenderlovemaking.com/).*

[tcc-lsra]: /assets/img/tcc-linearscan-ra.pdf

[quality-lsra]: /assets/img/quality-speed-linear-scan-ra-clean.pdf

[lsra]: /assets/img/linearscan-ra.pdf

[lsra-ssa]: /assets/img/wimmer-linear-scan-ssa.pdf

[lsra-context-ssa]: /assets/img/linear-scan-ra-context-ssa.pdf

[ra-programs-ssa]: /assets/img/ra-programs-ssa.pdf

[ssa-elim-after-ra]: /assets/img/ssa-elimination-after-ra.pdf

[register-spilling-range-splitting-ssa]: /assets/img/register-spilling-range-splitting-ssa.pdf

[wimmer-masters-thesis]: /assets/img/wimmer-masters-thesis.pdf

[optimized-interval-splitting]: /assets/img/optimized-interval-splitting-linear-scan-ra.pdf

[parallel-move-leroy]: /assets/img/parallel-move-leroy.pdf

[boissinot-out-ssa]: /assets/img/boissinot-out-ssa.pdf

[boissinot-thesis]: /assets/img/boissinot-thesis.pdf

The fundamental problem in register allocation is to take an IR that uses a
virtual registers (as many as you like) and rewrite it to use a finite amount
of physical registers and stack space[^calendaring].

[^calendaring]: It's not just about registers, either. In 2016, Facebook
    engineer Dave [legendarily used linear-scan register allocation to book
    meeting rooms](https://blog.waleedkhan.name/will-i-ever-use-this/).

This is an example of a code snippet using virtual registers:

```
add R1, R2 -> R3
add R1, R3 -> R4
ret R4
```

And here is the same example after it has been passed through a register
allocator (note that Rs changed to Ps):

```
add Stack[0], P0 -> P1
add Stack[0], P1 -> P0
ret
```

Each virtual register was assigned a physical place: R1 to the stack, R2 to P0,
R3 to P1, and R4 *also* to P0 (since we weren't using R2 anymore).

People use register allocators like they use garbage collectors: it's an
abstraction that can manage your resources for you, maybe with some cost. When
writing the back-end of a compiler, it's probably much easier to have a
separate register-allocator-in-a-box than manually managing variable lifetimes
while also considering all of your different target architectures.

How do JIT compilers do register allocation? Well, "everyone knows" that "every
JIT does its own variant of linear scan"[^everyone]. This bothered me for some
time because I've worked on a couple of JITs and still didn't understand the
backend bits.

[^everyone]: Well. As I said on one of the social media sites earlier this
    year, "All AOT compilers are alike; each JIT compiler is fucked up in its
    own way."

    JavaScript:

    <!-- * V8's Maglev uses -->
    * V8's TurboFan uses [linear scan](https://github.com/v8/v8/blob/12fa27f2f4d999320c524776ed29810c8694bafc/src/compiler/backend/register-allocator.h#L1545)
    * SpiderMonkey uses a [backtracking allocator](https://searchfox.org/mozilla-central/rev/c85c168374483a3c37aab49d7f587ea74a516492/js/src/jit/BacktrackingAllocator.h#28-31)
      based on [LLVM's](https://blog.llvm.org/2011/09/greedy-register-allocation-in-llvm-30.html)
    * JavaScriptCore uses [linear scan "for
      optLevel<2"](https://github.com/WebKit/WebKit/blob/f5a9393bdeff7c89685de21aa9f2df392139cc07/Source/JavaScriptCore/b3/air/AirAllocateRegistersAndStackByLinearScan.h#L37)
      and [graph coloring otherwise](https://github.com/WebKit/WebKit/blob/f5a9393bdeff7c89685de21aa9f2df392139cc07/Source/JavaScriptCore/b3/air/AirAllocateRegistersByGraphColoring.h)
      * There's also a "cssjit" with its own register allocator...

    Java:

    * HotSpot C1 uses (naturally) [Wimmer2010 linear scan](https://github.com/openjdk/jdk/blob/87d734012e3130501bfd37b23cee7f5e0a3a476f/src/hotspot/share/c1/c1_LinearScan.hpp)
    * HotSpot C2 uses [Chaitin-Briggs-Click graph coloring](https://github.com/openjdk/jdk/blob/87d734012e3130501bfd37b23cee7f5e0a3a476f/src/hotspot/share/opto/regalloc.hpp)
    * GraalVM uses [linear scan](https://github.com/oracle/graal/blob/e482f988939235ce94ee4a756c6bcc1d3df2bab2/compiler/src/jdk.graal.compiler/src/jdk/graal/compiler/lir/alloc/lsra/LinearScan.java)
    * <!-- Dalvik, ART -->

    Python:

    <!-- * PyPy uses -->
    * Cinder uses [Wimmer2010 linear scan](https://github.com/facebookincubator/cinderx/blob/5cf14ad8a68b6f04c1ca1cb99947da7d8d09c28b/cinderx/Jit/lir/regalloc.h)
    * S6 uses a [trace register allocator](https://github.com/google-deepmind/s6/blob/69cac9c981fbd3217ed117c3898382cfe094efc0/src/code_generation/trace_register_allocator.h)
      * This is a different thing than a tracing JIT; see [Josef Eisl's thesis](/assets/img/trace-ra.pdf) (PDF, 2018)

    Ruby:

    * YJIT uses [linear scan](https://github.com/ruby/ruby/blob/231407c251d82573f578caf569a934c0ebb344e5/yjit/src/backend/ir.rs#L1388)
    * ZJIT uses more or less the same backend, so also linear scan

    PHP:

    * PHP uses [linear scan](https://github.com/php/php-src/blob/77dace78c324ef731e60fa98b4b8008cd7df1657/ext/opcache/jit/ir/ir_ra.c#L3479)
    * HHVM uses [extended linear scan](https://github.com/facebook/hhvm/blob/e7bca518648e16bdb7c08e91d02f8c158d8e6c6f/hphp/runtime/vm/jit/vasm-xls.cpp#L1448)

    Lua:

    * LuaJIT uses [reverse linear scan][luajit-lsra]

There are a couple different approaches to register allocation, but in this
post we'll focus on *linear scan of SSA*.

I started reading [Linear Scan Register Allocation on SSA Form][lsra-ssa] (PDF,
2010) by Wimmer and Franz after writing [A catalog of ways to generate
SSA](/blog/ssa/). Reading alone didn't make a ton of sense---I ended up with a
lot of very frustrated margin notes. I started trying to implement it alongside
the paper. As it turns out, though, there is a rich history of papers in this
area that it leans on really heavily. I needed to follow the chain of
references!

> For example, here is a lovely explanation of the process, start to finish,
> from Christian Wimmer's [Master's thesis][wimmer-masters-thesis] (PDF, 2004).
>
> ```
> LINEAR_SCAN
>   // order blocks and operations (including loop detection)
>   COMPUTE_BLOCK_ORDER
>   NUMBER_OPERATIONS
>   // create intervals with live ranges
>   COMPUTE_LOCAL_LIVE_SETS
>   COMPUTE_GLOBAL_LIVE_SETS
>   BUILD_INTERVALS
>   // allocate registers
>   WALK_INTERVALS
>   RESOLVE_DATA_FLOW
>   // replace virtual registers with physical registers
>   ASSIGN_REG_NUM
>   // special handling for the Intel FPU stack
>   ALLOCATE_FPU_STACK
> ```
>
> There it is, all laid out at once. It's very refreshing when compared to all
> of the compact research papers.

I didn't realize that there were more than one or two papers on linear scan. So
this post will also incidentally serve as a bit of a survey or a history of
linear scan---as best as I can figure it out, anyway. If you were in or near
the room where it happened, please feel free to reach out and correct some
parts.

## Some example code

Throughout this post, we'll use an example SSA code snippet from Wimmer2010,
adapted from phi-SSA to block-argument-SSA. Wimmer2010's code snippet is
between the arrows and we add some filler (as alluded to in the paper):

```
label B1(R10, R11):
jmp B2($1, R11)
 # vvvvvvvvvv #
label B2(R12, R13)
cmp R13, $1
branch lessThan B4()

label B3()
mul R12, R13 -> R14
sub R13, $1 -> R15
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
<TR><TD PORT="params" BGCOLOR="lightgray">B1(R10, R11)&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">jump →B2($1, R11)&nbsp;</TD></TR>
</TABLE>>];
B1:0 -> B2:params;
B2 [label=<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0">
<TR><TD PORT="params" BGCOLOR="lightgray">B2(R12, R13)&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">cmp R13, $1&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="1">blt →B4, →B3&nbsp;</TD></TR>
</TABLE>>];
B2:1 -> B4:params;
B2:1 -> B3:params;
B3 [label=<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0">
<TR><TD PORT="params" BGCOLOR="lightgray">B3()&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">R14 = mul R12, R13&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="1">R15 = sub R13, $1&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="2">jump →B2(R14, R15)&nbsp;</TD></TR>
</TABLE>>];
B3:2 -> B2:params;
B4 [label=<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0">
<TR><TD PORT="params" BGCOLOR="lightgray">B4()&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">R16 = add R10, R12&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="1">ret R16&nbsp;</TD></TR>
</TABLE>>];
}
-->
<figure>
<object class="svg" type="image/svg+xml" data="/assets/img/wimmer-lsra-cfg.svg"></object>
<figcaption markdown=1>
We have one entry block, `B1`, that is implied in
Wimmer2010. Its only job is to define `R10` and `R11` for the rest of the CFG.

Then we have a loop between `B2` and `B3` with an implicit fallthrough. Instead
of doing that, we instead generate a conditional branch with explicit jump
targets. This makes it possible to re-order blocks as much as we like.

The contents of `B4` are also just to fill in the blanks from Wimmer2010 and
add some variable uses.
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
High-level Dynamic Code Generation][tcc-lsra] (PDF, 1997) by Poletto, Engler,
and Kaashoek. (Until writing this post, I had never seen this paper. It was
only on a re-read of the 1999 paper (below) that I noticed it.) In this paper,
they mostly describe a staged variant of C called 'C (TickC), for which a fast
register allocator is quite useful.

Then came a paper called [Quality and Speed in Linear-scan Register
Allocation][quality-lsra] (PDF, 1998) by Traub, Holloway, and Smith. It adds
some optimizations (lifetime holes, binpacking) to the algorithm presented in
Poletto1997.

Then came the first paper I read, and I think the paper everyone refers to when
they talk about linear scan: [Linear Scan Register Allocation][lsra] (PDF,
1999) by Poletto and Sarkar. In this paper, they give a fast alternative to
graph coloring register allocation, especially motivated by just-in-time
compilers. In retrospect, it seems to be a bit of a rehash of the previous two
papers.

Linear scan (1997, 1999) operates on *live ranges* instead of virtual
registers. A live range is a pair of integers [start, end) (end is exclusive)
that begins when the register is defined and ends when it is last used. This
means that there is an assumption that the order for instructions in your
program has already been fixed into a single linear sequence! It also means
that you have given each instruction a number that represents its position in
that order.

> This may or not be a surprising requirement depending on your compilers
> background. It was surprising to me because I often live in control flow
> graph fantasy land where blocks are unordered and instructions sometimes
> float around. But if you live in a land of basic blocks that are already in
> reverse post order, then it may be less surprising.

In non-SSA-land, these live ranges are different from the virtual registers:
they represent some kind of lifetimes of each *version* of a virtual register.
For an example, consider the following code snippet:

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
register allocator[^allocate-on-ssa].

[^allocate-on-ssa]: [Linear Scan Register Allocation in the Context of SSA Form
    and Register Constraints][lsra-context-ssa] (PDF, 2002) by Mössenböck and
    Pfeiffer notes:

    > Our allocator relies on static single assignment form, which simplifies
    > data flow analysis and tends to produce short live intervals.

    [Register allocation for programs in SSA-form][ra-programs-ssa] (PDF, 2006)
    by Hack, Grund, and Goos notes that interference graphs for SSA programs
    are chordal and can be optimally colored in quadratic time.

    [SSA Elimination after Register Allocation][ssa-elim-after-ra] (PDF, 2008)
    by Pereira and Palsberg notes:

    > One of the main advantages of SSA based register allocation is the
    > separation of phases between spilling and register assignment.

    Cliff Click (private communication, 2025) notes:

    > It's easier. Got it already, why lose it [...] spilling always uses
    > use/def and def/use edges.

Linear scan starts at the point in your compiler process where you already know
these live ranges---that you have already done some kind of analysis to build a
mapping.

In this blog post, we're going to back up to the point where we've just built
our SSA low-level IR and have yet to do any work on it. We'll do all of the
analysis from scratch.

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

This live-in set becomes B1's *live-out* set because B1 is B2's predecessor. We
continue in B1. We could continue backwards linearly through the blocks. In
fact, I encourage you to do it as an exercise. You should have a (potentially
emtpy) set of registers per basic block.

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
range building, which we'll instead do separately in a moment.

The Wimmer approach sounded complicated and finicky. Maybe it is, maybe it
isn't. So I went with a dataflow liveness analysis instead. If it turns out to
be the slow part, maybe it will matter enough to learn about this loop tagging
method.

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
* We only give out even numbers because later we'll insert loads and stores at
  odd-numbered instructions
  * Cinder does this to [separately identify instruction input and instruction output](https://github.com/facebookincubator/cinderx/blob/2b8774f077d6ef441207067411d157bb4f94a40b/cinderx/Jit/lir/regalloc.cpp#L243)

Even though we have extra instructions, it looks very similar to the example in
the Wimmer2010 paper.

```
16: label B1(R10, R11):
18: jmp B2($1, R11)
     # vvvvvvvvvv #
20: label B2(R12, R13)
22: cmp R13, $1
24: branch lessThan B4()

26: label B3()
28: mul R12, R13 -> R14
30: sub R13, $1 -> R15
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
you with a function called `build_intervals`? That's because early in
the history of linear scan (Traub1998!), people moved from having a single range for a
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

```
BUILDINTERVALS
for each block b in reverse order do
  live = union of successor.liveIn for each successor of b
  for each phi function phi of successors of b do
    live.add(phi.inputOf(b))
  for each opd in live do
    intervals[opd].addRange(b.from, b.to)
  for each operation op of b in reverse order do
    for each output operand opd of op do
      intervals[opd].setFrom(op.id)
      live.remove(opd)
    for each input operand opd of op do
      intervals[opd].addRange(b.from, op.id)
      live.add(opd)
  for each phi function phi of b do
    live.remove(phi.output)
  if b is loop header then
    loopEnd = last block of the loop starting at b
    for each opd in live do
      intervals[opd].addRange(b.from, loopEnd.to)
  b.liveIn = live
```

Anyway, here is the mostly-copied annotated implementation of BuildIntervals
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
      if @range.end <= from
        raise ArgumentError, "Invalid range: #{from} to #{@range.end}"
      end
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

Note that there's some implicit behavior happening here:

* If we haven't initialized a range yet, we build one automatically
* If we have a range, `add_range` builds the smallest range that overlaps with
  the existing range and incoming information
* If we have a range, `set_from` may shrink it

For example, if we have `[1, 5)` and someone calls `add_range(7, 10)`, we end
up with `[1, 10)`. There's no gap in the middle.

And if we have `[1, 7)` and someone calls `set_from(3)`, we end up with `[3,
7)`.

After figuring out from scratch some of these assumptions about what the
interval/range API should and should not do, Aaron and I realized that there
was some actual code for `add_range` in a different, earlier paper: [Linear
Scan Register Allocation in the Context of SSA Form and Register
Constraints][lsra-context-ssa] (PDF, 2002) by Mössenböck and Pfeiffer.

```
ADDRANGE(i: Instruction; b: Block; end: integer)
  if b.first.n ≤ i.n ≤ b.last.n then range ← [i.n, end[ else range ← [b.first.n, end[
  add range to interval[i.n] // merging adjacent ranges
```

Unfortunately, many other versions of this PDF look absolutely horrible (like
bad OCR) and I had to do some digging to find the version linked above.

Finally we can start thinking about doing some actual register assignment.
Let's return to the 90s.

## Linear scan

Because we have faithfully kept 1 interval == 1 range, we can re-use the linear
scan algorithm from Poletto1999 (which looks, at a glance, to be the same
as 1997).

I recommend looking at the PDF side by side with the code. We have tried to
keep the structure very similar.

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
        if active_interval.range.end > interval.range.begin
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
          # The last active interval ends further away than the current
          # interval; spill the last active interval.
          assignment[interval] = assignment[spill]
          raise "Should be assigned a register" unless assignment[interval].is_a?(PReg)
          assignment[spill] = slot
          active.pop  # We know spill is the last one
          # Insert interval into already-sorted active
          insert_idx = active.bsearch_index { |i| i.range.end >= interval.range.end } || active.length
          active.insert(insert_idx, interval)
        else
          # The current interval ends further away than the last active
          # interval; spill the current interval.
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

Internalizing this took us a bit. It is mostly a three-state machine:

* have not been allocated
* have been allocated a register
* have been allocated a stack slot

We would like to come back to this and incrementally modify it as we add
lifetime holes to intervals.

I finally understood, very late in the game, that linear scan assigns one
location per virtual register. *Ever*. It's not that every virtual register
gets a shot in a register and then gets moved to a stack slot---that would be
interval splitting and hopefully we get to that later---if a register gets
spilled, it's in a stack slot from beginning to end.

I only found this out accidentally after trying to figure out a bug (that
wasn't a bug) due to a lovely sentence in [Optimized Interval Splitting in a
Linear Scan Register Allocator][optimized-interval-splitting] (PDF, 2005) by
Wimmer and Mössenböck):

> However, it cannot deal with lifetime holes and does not split intervals, so
> an interval has either a register assigned for the whole lifetime, or it is
> spilled completely.

Also,

> In particular, it is not possible to implement the algorithm without
> reserving a scratch register: When a spilled interval is used by an
> instruction requiring the operand in a register, the interval must be
> temporarily reloaded to the scratch register

Also,

> Additionally, register constraints for method calls and instructions
> requiring fixed registers must be handled separately

Marvelous.

Let's take a look at the code snippet again. Here it is before register
allocation, using virtual registers:

```
16: label B1(R10, R11):
18: jmp B2($1, R11)
     # vvvvvvvvvv #
20: label B2(R12, R13)
22: cmp R13, $1
24: branch lessThan B4()

26: label B3()
28: mul R12, R13 -> R14
30: sub R13, $1 -> R15
32: jump B2(R14, R15)

34: label B4()
     # ^^^^^^^^^^ #
36: add R10, R12 -> R16
38: ret R16
```

Let's run it through register allocation with incrementally decreasing numbers
of physical registers available. We get the following assignments:

* 4 registers `{R10: P0, R11: P1, R12: P1, R13: P2, R14: P3, R15: P2, R16: P0}`
* 3 registers `{R10: Stack[0], R11: P1, R12: P1, R13: P2, R14: P0, R15: P2, R16: P0}`
* 2 registers `{R10: Stack[0], R11: P1, R12: Stack[1], R13: P0, R14: P1, R15: P0, R16: P0}`
* 1 register `{R10: Stack[0], R11: P0, R12: Stack[1], R13: P0, R14: Stack[2], R15: P0, R16: P0}`

Some other things to note:

* If you have a register free, choosing which register to allocate is a
  heuristic! It is tunable. There is probably some research out there that
  explores the space.

  In fact, you might even consider *not* allocating a register greedily. What
  might that look like? I have no idea.
* Spilling the interval with the furthest endpoint is a heuristic! You can
  pick any active interval you want. In [Register Spilling and Live-Range
  Splitting for SSA-Form Programs][register-spilling-range-splitting-ssa] (PDF,
  2009) by Braun and Hack, for example, they present the MIN algorithm, which
  spills the interval with the furthest next use.

  This requires slightly more information and takes slightly more time than
  the default heuristic but apparently generates much better code.
* Also, block ordering? You guessed it. Heuristic.

Here is an example "slideshow" I generated by running linear scan with 2
registers. Use the arrow keys to navigate forward and backward in time[^rsms].

[^rsms]: This is inspired by [Rasmus Andersson](https://rsms.me/)'s graph
    coloring [visualization](https://rsms.me/projects/chaitin/) that I saw some
    years ago.

<iframe src="/assets/lsra.html" width="100%" onload="this.style.height = this.contentWindow.document.documentElement.scrollHeight + 'px';"></iframe>

## Resolving SSA

At this point we have register *assignments*: we have a hash table mapping
intervals to physical locations. That's great but we're still in SSA form:
labelled code regions don't have block arguments in hardware. We need to write
some code to take us out of SSA and into the real world.

We can use a modified Wimmer2010 as a great start point here. It handles more
than we need to right now---lifetime holes---but we can simplify.

```
RESOLVE
for each control flow edge from predecessor to successor do
  for each interval it live at begin of successor do
    if it starts at begin of successor then
      phi = phi function defining it
      opd = phi.inputOf(predecessor)
      if opd is a constant then
        moveFrom = opd
      else
        moveFrom = location of intervals[opd] at end of predecessor
    else
      moveFrom = location of it at end of predecessor
    moveTo = location of it at begin of successor
    if moveFrom ≠ moveTo then
      mapping.add(moveFrom, moveTo)
  mapping.orderAndInsertMoves()
```

Because we have a 1:1 mapping of virtual registers to live ranges, we know that
every interval live at the beginning of a block is either:

* live across an edge between two blocks and therefore has already been placed
  in a location by assignment/spill code
* beginning its life at the beginning of the block as a block parameter and
  therefore needs to be moved from its source location

For this reason, we only handle the second case in our SSA resolution. If we
added lifetime holes, we would have to go back to the full Wimmer SSA
resolution.

This means that we're going to iterate over every outbound edge from every
block. For each edge, we're going to insert some parallel moves.

```ruby
class Function
  def resolve_ssa intervals, assignments
    # ...
    @block_order.each do |predecessor|
      outgoing_edges = predecessor.edges
      num_successors = outgoing_edges.length
      outgoing_edges.each do |edge|
        mapping = []
        successor = edge.block
        edge.args.zip(successor.parameters).each do |moveFrom, moveTo|
          if moveFrom != moveTo
            mapping << [moveFrom, moveTo]
          end
        end
        # predecessor.order_and_insert_moves(mapping)
        # TODO: order_and_insert_moves
      end
    end
    # Remove all block parameters and arguments; we have resolved SSA
    @block_order.each do |block|
      block.parameters.clear
      block.edges.each do |edge|
        edge.args.clear
      end
    end
  end
end
```

This already looks very similar to the RESOLVE function from Wimmer2010.
Unfortunately, Wimmer2010 basically shrugs off `orderAndInsertMoves` with an *eh, it's
already in the literature* comment.

### A brief and frustrating parallel moves detour

What's not made clear, though, is that this particular subroutine has been the
source of a significant amount of bugs in the literature. Only recently did
some folks roll through and suggest (proven!) fixes:

* [Battling windmills with Coq: formal verification of a compilation algorithm
  for parallel moves][parallel-move-leroy] (PDF, 2007) by Rideau, Serpette, and
  Leroy
* [Revisiting Out-of-SSA Translation for Correctness, Code Quality, and
  Efficiency][boissinot-out-ssa] (PDF, 2009) by Boissinot, Darte, Rastello,
  Dupont de Dinechin, and Guillon.
  * and again in [Boissinot's thesis][boissinot-thesis] (PDF, 2010)

This sent us on a deep rabbit hole of trying to understand what bugs occur,
when, and how to fix them. We implemented both the Leroy and the Boissinot
algorithms. We found differences between Boissinot2009, Boissinot2010, and the
SSA book implementation following those algorithms. We found Paul Sokolovsky's
[implementation with bugfixes](https://github.com/pfalcon/parcopy/). We found
Dmitry Stogov's [unmerged pull
request](https://github.com/pfalcon/parcopy/pull/1) to the same repository to
fix another bug.

We looked at Benoit Boissinot's thesis again and emailed him some questions. He
responded! And then he even put up an [amended version of his
algorithm](https://github.com/bboissin/thesis_bboissin) in Rust with tests and
fuzzing.

All this is to say that this is still causing people grief and, though I
understand page limits, I wish parallel moves were not handwaved away.

We ended up with this implementation which passes all of the tests from
Sokolovsky's repository as well as the example from Boissinot's thesis (though,
as we discussed in the email, the example solution in the thesis is
incorrect[^thesis-correction]).

[^thesis-correction]: The example in the thesis is to sequentialize the
    following parallel copy:

    * a &rarr; b
    * b &rarr; c
    * c &rarr; a
    * c &rarr; d

    The solution in the thesis is:

    1. c &rarr; d (c now lives in d)
    2. a &rarr; c (a now lives in c)
    3. b &rarr; a (b now lives in a)
    4. d &rarr; b (why are we copying c to b?)

    but we think this is incorrect. Solving manually, Aaron and I got:

    1. c &rarr; d (because d is not read from anywhere)
    2. b &rarr; c (because c is "freed up"; now in d)
    3. a &rarr; b (because b is "freed up"; now in c)
    4. d &rarr; a (because c is now in d, so d &rarr; a is equivalent to old\_c &rarr; a)

    which is what the code gives us, too.

```ruby
# copies contains an array of [src, dst] arrays
def sequentialize copies
  ready = []  # Contains only destination regs ("available")
  to_do = []  # Contains only destination regs
  pred = {}  # Map of destination reg -> what reg is written to it (its source)
  loc = {}  # Map of reg -> the current location where the initial value of reg is available ("resource")
  result = []

  emit_copy = -> (src, dst) {
    # We add an arrow here just for clarity in reading this algorithm because
    # different people do [src, dst] and [dst, src] depending on if they prefer
    # Intel or AT&T
    result << [src, "->", dst]
  }

  # In Ruby, loc[x] is nil if x not in loc, so this loop could be omitted
  copies.each do |(src, dst)|
    loc[dst] = nil
  end

  copies.each do |(src, dst)|
    loc[src] = src
    if pred.key? dst  # Alternatively, to_do.include? dst
      raise "Conflicting assignments to destination #{dst}, latest: #{[dst, src]}"
    end
    pred[dst] = src
    to_do << dst
  end

  copies.each do |(src, dst)|
    if !loc[dst]
      # All destinations that are not also sources can be written to immediately (tree leaves)
      ready << dst
    end
  end

  while !to_do.empty?
    while b = ready.pop
      a = loc[pred[b]] # a in the paper
      emit_copy.(a, b)
      # pred[b] is now living at b
      loc[pred[b]] = b
      if to_do.include?(a)
        to_do.delete a
      end
      if pred[b] == a && pred.include?(a)
        ready << a
      end
    end

    if to_do.empty?
      break
    end

    dst = to_do.pop
    if dst != loc[pred[dst]]
      emit_copy.(dst, "tmp")
      loc[dst] = "tmp"
      ready << dst
    end
  end
  result
end
```

Leroy's algorithm, which is shorter, passes almost all the tests---in one test
case, it uses one more temporary variable than Boissinot's does. We haven't
spent much time looking at why.

```ruby
def move_one i, src, dst, status, result
  return if src[i] == dst[i]
  status[i] = :being_moved
  for j in 0...(src.length) do
    if src[j] == dst[i]
      case status[j]
      when :to_move
        move_one j, src, dst, status, result
      when :being_moved
        result << [src[j], "->", "tmp"]
        src[j] = "tmp"
      end
    end
  end
  result << [src[i], "->", dst[i]]
  status[i] = :moved
end

def leroy_sequentialize copies
  src = copies.map { it[0] }
  dst = copies.map { it[1] }
  status = [:to_move] * src.length
  result = []
  status.each_with_index do |item, i|
    if item == :to_move
      move_one i, src, dst, status, result
    end
  end
  result
end
```

### Back to SSA resolution

Whatever algorithm you choose, you now have a way to parallel move some
registers to some other registers. You have avoided the "swap problem".

```ruby
class Function
  def resolve_ssa intervals, assignments
    # ...
        # predecessor.order_and_insert_moves(mapping)
        sequence = sequentialize(mapping).map do |(src, _, dst)|
          Insn.new(:mov, dst, [src])
        end
        # TODO: insert the moves!
    # ...
  end
end
```

That's great. You can generate an ordered list of instructions from a tangled
graph. But where do you put them? What about the "lost copy" problem?

As it turns out, we still need to handle critical edge splitting. Let's
consider what it means to insert moves at an edge between blocks `A -> B` when
the surrounding CFG looks a couple of different ways.

* Case 1: `A -> B`
* Case 2: `A -> B` and `A -> C`
* Case 3: `A -> B` and `D -> B`
* Case 4: `A -> B` and `A -> C` and `D -> B`

These are the four (really, three) cases we may come across.

In Case 1, if we only have two neighboring blocks A and B, we can
insert the moves into either block. It doesn't matter: at the end of A or at
the beginning of B are both fine.

In Case 2, if A has two successors, then we should insert the moves at the
beginning of B. That way we won't be mucking things up for the edge `A -> C`.

In Case 3, if B has two predecessors, then we should insert the moves at the
end of A. That way we won't be mucking things up for the edge `D -> B`.

Case 4 is the most complicated. There is no extant place in the graph we can
insert moves. If we insert in A, we mess things up for `A -> C`. If we insert
in `B`, we mess things up for `D -> B`. Inserting in `C` or `D` doesn't make
any sense. What is there to do?

As it turns out, Case 4 is called a *critical edge*. And we have to split it.

We can insert a new block E along the edge `A -> B` and put the moves in E!
That way they still happen along the edge without affecting any other blocks.
Neat.

In Ruby code, that looks like:

```ruby
class Function
  def resolve_ssa intervals, assignments
    num_predecessors = Hash.new 0
    @block_order.each do |block|
      block.edges.each do |edge|
        num_predecessors[edge.block] += 1
      end
    end
    # ...
        # predecessor.order_and_insert_moves(mapping)
        sequence = ...
        # If we don't have any moves to insert, we don't have any block to
        # insert
        next if sequence.empty?
        if num_predecessors[successor] > 1 && num_successors > 1
          # Make a new interstitial block
          b = new_block
          b.insert_moves_at_start sequence
          b.instructions << Insn.new(:jump, nil, [Edge.new(successor, [])])
          edge.block = b
        elsif num_successors > 1
          # Insert into the beginning of the block
          successor.insert_moves_at_start sequence
        else
          # Insert into the end of the block... before the terminator
          predecessor.insert_moves_at_end sequence
        end
    # ...
  end
end
```

Adding a new block invalidates the cached `@block_order`, so we also need to
recompute that.

> We could also avoid that by splitting critical edges earlier, before
> numbering. Then, when we arrive in `resolve_ssa`, we can clean up branches to
> empty blocks!

(See also [Nick's post on critical edge
splitting](https://nickdesaulniers.github.io/blog/2023/01/27/critical-edge-splitting/),
which also links to Faddegon's thesis, which I should at least skim.)

And that's it, folks. We have gone from virtual registers in SSA form to
physical locations. Everything's all hunky-dory. We can just turn these LIR
instructions into their very similar looking machine equivalents, right?

Not so fast...

## Calls

You may have noticed that the original linear scan paper does not mention calls
or other register constraints. I didn't really think about it until I wanted to
make a function call. The authors of later linear scan papers definitely
noticed, though; Wimmer2005 writes the following about Poletto1999:

> When a spilled interval is used by an instruction requiring the operand in a
> register, the interval must be temporarily reloaded to the scratch register.
> Additionally, register constraints for method calls and instructions
> requiring fixed registers must be handled separately.

Fun. We will start off by handling calls and method parameters separately, we
will note that it's not amazing code, and then we will eventually implement the
later papers, which handle register constraints more naturally.

We'll call this new function `handle_caller_saved_regs` after register
allocation but before SSA resolution. We do it after register allocation so we
know where each virtual register goes but before resolution so we can still
inspect the virtual register operands.

Its goal is to do a couple of things:

* Insert special `push` and `pop` instructions around `call` instructions to
  preserve virtual registers that are used on the other side of the `call`. We
  only care about preserving virtual registers that are stored in physical
  registers, though; no need to preserve anything that already lives on the
  stack.
* Do a parallel move of the call arguments into the ABI-specified parameter
  registers. We need to do a parallel move in case any of the arguments happen
  to already be living in parameter registers. (We're really getting good
  mileage out of this function.)
* Make sure that the value returned by the call in the ABI-specified return
  register ends up in in the location allocated to the output of the `call`
  instruction.

We'll also remove the `call` operands since we're placing them in special
registers explicitly now.

```ruby
class Function
  def handle_caller_saved_regs intervals, assignments, return_reg, param_regs
    @block_order.each do |block|
      x = block.instructions.flat_map do |insn|
        if insn.name == :call
          survivors = intervals.select { |_vreg, interval|
            interval.survives?(insn.number)
          }.map(&:first).select { |vreg|
            assignments[intervals[vreg]].is_a?(PReg)
          }
          mov_input = insn.out
          insn.out = return_reg

          ins = insn.ins.drop(1)
          raise if ins.length > param_regs.length

          insn.ins.replace(insn.ins.first(1))

          mapping = ins.zip(param_regs).to_h
          sequence = sequentialize(mapping).map do |(src, _, dst)|
            Insn.new(:mov, dst, [src])
          end

          survivors.map { |s| Insn.new(:push, nil, [s]) } +
            sequence +
            [insn, Insn.new(:mov, mov_input, [return_reg])] +
            survivors.map { |s| Insn.new(:pop, nil, [s]) }.reverse
        else
          insn
        end
      end
      block.instructions.replace(x)
    end
  end
end
```

(Unfortunately, this sidesteps handling the less-fun bit of calls in ABIs where
after the 6th parameter, they are expected on the stack. It also completely
ignores ABI size constraints.)

Now, you may have noticed that we don't do anything special for the incoming
params of the function we're compiling! That's another thing we have to handle.
Thankfully, we can handle it with yet another parallel move (wow!) at the end
of `resolve_ssa`.

```ruby
class Function
  def resolve_ssa intervals, assignments
    # ...
    # We're typically going to have more param regs than block parameters
    # When we zip the param regs with block params, we'll end up with param
    # regs mapping to nil. We filter those away by selecting for tuples
    # that have a truthy second value
    # [[x, y], [z, nil]].select(&:last) (reject the second tuple)
    mapping = param_regs.zip(entry_block.parameters).select(&:last).to_h
    sequence = sequentialize(mapping).map do |(src, _, dst)|
      Insn.new(:mov, dst, [src])
    end
    entry_block.insert_moves_at_start(sequence)
  end
end
```

Again, this is yet another kind of thing where some of the later papers have
much better ergonomics and also much better generated code.

But this is really cool! If you have arrived at this point with me, we have
successfully made it to 1997 and that is nothing to sneeze at. We have even
adapted research from 1997 to work with SSA, avoiding several significant
classes of bugs along the way.

<!--
## Instruction selection and instruction splitting

## Lifetime holes and interval splitting

## Register hints

What is this iterated linear scan thing? Appears in JSC

while (true) {
  linearscan();
  if (!would_spill) { break; }
  interval = pick_an_interval_to_spill();
  spill(interval);
  remove_interval(interval);
}
-->

## Validation by abstract interpretation

We have just built an enormously complex machine. Even out the gate, with the
original linear scan, there is a lot of machinery. It's possible to write tests
that spot check sample programs of all shapes and sizes but it's *very*
difficult to anticipate every possible edge case that will appear in the real
world.

Even if the original algorithm you're using has been proven correct, your
implementation may have subtle bugs due to (for example) having slightly
different invariants or even transcription errors.

We have all these proof tools at our disposal: we can write an abstract
interpreter that verifies properties of *one* graph, but it's very hard
(impossible?) to scale that to sets of graphs.

Maybe that's enough, though. In one of my favorite blog posts, Chris Fallin
[writes about](https://cfallin.org/blog/2021/03/15/cranelift-isel-3/) writing a
register allocation verifier based on abstract interpretation. It can verify
one concrete LIR function at a time. It's fast enough that it can be left on in
debug builds. This means that a decent chunk of the time (tests, CI, maybe a
production cluster) we can get a very clear signal that every register
assignment that passes through the verifier satisfies some invariants.

Furthermore, we are not limited to Real World Code. With the advent of fuzzing,
one can imagine an always-on fuzzer that tries to break the register allocator.
A verifier can then catch bugs that come from exploring this huge search space.

Some time after finding Chris's blog post, I also stumbled across [the very same
thing in
V8](https://github.com/v8/v8/blob/cac6de03372c25987c6cbea49b4b39d9da437978/src/compiler/backend/register-allocator-verifier.h)!

I find this stuff so cool. I'll also mention Boissinot's [Rust
code](https://github.com/bboissin/thesis_bboissin) again because it does
something similar for parallel moves.

## See also

It's possible to do linear scan allocation in reverse, at least on traces
without control-flow. See for example [The Solid-State Register
Allocator](https://www.mattkeeter.com/blog/2022-10-04-ssra/), the [LuaJIT
register allocator][luajit-lsra], and [Reverse Linear Scan Allocation is
probably a good idea](https://brrt-to-the-future.blogspot.com/2019/03/reverse-linear-scan-allocation-is.html).
By doing linear scan this way, it is also possible to avoid computing liveness
and intervals. I am not sure if this works on programs with control-flow,
though.

[luajit-lsra]: https://github.com/LuaJIT/LuaJIT/blob/5e3c45c43bb0e0f1f2917d432e9d2dba12c42a6e/src/lj_asm.c#L198

## Wrapping up

We built a register allocator that works on SSA. Hopefully next time we will
add features such as lifetime holes, interval splitting, and register hints.

The full Ruby code listing is ~~not (yet?) public~~ [available under the Apache
2 license](https://github.com/tenderworks/regalloc).

## Thanks

Thanks to [Waleed Khan](https://waleedkhan.name/) and [Iain
Ireland](https://mstdn.ca/@iainireland) for giving feedback on this post.

---
title: "Notes on linear scan for SSA"
layout: post
---

<!--
I do a lot of talking about high-level IRs because a lot of my professional
experience is in JITS for dynamic languges such as Python and Ruby. With
languages like those, you need to get rid of a lot of dynamic operations by
speculating on types. That's best done in a high-level IR---an HIR, if you
will. HIR is very abstract and deals in the guest language types. When we're
done optimizing that, though, we need to make our IR more concrete again. For
that, we have LIR.

The LIRs I have worked with have been inf
-->

Reading through the linear scan paper by Wimmer and Franz and taking notes.
Assumed knowledge:

* SSA Control-flow graphs
* Static analysis (in particular, abstract interpretation)

I have only briefly skimmed the original linear scan paper and not understood
it so this is really the first register allocation paper I am going to try and
fully internalize.

## Abstract

Promises:

* Linear scan on SSA instead of SSA destruction first
* Construct lifetime intervals without dataflow analysis
* Skip some interval intersection tests because "SSA form guarantees
  non-intersection"
* Integrate SSA destruction into normal linear scan "resolution" phase without
  much additional code

## 1. Introduction

SSA assumes phi functions. I want to also think about this from the perspective
of basic block arguments, which I (maybe naively) assume gets rid of critical
edge splitting problems

SSA interference graph is chordal, chordal graphs can be colored in polynomial
time, and this simplifies graph coloring allocators because it lets authors
split spilling and register assignment algorithms. Not super obvious to me why
that immediately follows or why it helps

* SSA makes lifetime intervals easier to construct
* SSA makes lifetime intervals have a simpler structure
* Linear scan already has some infrastructure to make SSA destruction easier

## 2. Overview

> At first, most operands are virtual registers. Only register constraints of
> the target architecture are modeled using physical registers in the initial
> LIR.

I guess this means that parameters get passed in RDI, RSI, ..., and return
values go in RAX, RDX, and ...

> Before register allocation, the control flow graph is flattened to a list of blocks.

I guess this is called scheduling. Interesting that blocks are preserved

> The register allocator replaces all virtual registers with physical
> registers, thereby inserting code for spilling registers to the stack if more
> values are simultaneously live than registers are available.

Sounds like a register allocator

> This is accomplished by splitting lifetime intervals, which requires a
> resolution phase after register allocation to insert move instructions at
> control flow edges.

I don't think I understood before now that interval splitting is baked in and
not one of the many add-ons that have cropped up over the years. Or is that
live range splitting? What is the difference?

Figure 2:

* mentions "Lifetime Analysis" right next to "No Data Flow Analysis", which is
  a little confusing
* mentions "Lifetime Intervals with Lifetime Holes", but does not explain what
  a lifetime hole is (yet?)

They have a comment about how "if SSA is not required after register
allocation, [...], but I thought the whole point was to get out of SSA? What
would phi functions merge? Physical registers, somehow?

## 3. Lifetime Intervals and SSA Form

> Our variant of the linear scan algorithm requires exact lifetime information:
> The lifetime interval of a virtual register must cover all parts where this
> register is needed, with lifetime holes in between.

I don't know what this is contrasting with. What other kinds of lifetime
information are there? No holes? Live for the whole program?

> Lifetime holes occur because the control flow graph is reduced to a list of
> blocks before register allocation. If a register flows into an else-block,
> but not into the corresponding if-block, the lifetime interval has a hole for
> the if-block. 

OK, nice. I guess this is better than (my limited understanding of) the
original linear scan in that regard?

> In contrast, a register defined before a loop and used inside the loop must
> be live in all blocks of the loop, even blocks after the last use.

I don't have a great idea of why extending the lifetime past the last use in
the loop is required

> [...] numbers are incremented by two for technical reasons [...]

This will be interesting

> [operations] are arithmetic and control flow operations that use up to two
> input operands (either virtual registers or constants) and define up to one
> output operand (a virtual register)

So unlike HIR, where a constant is a special instruction, constants can be
"inlined" into operations in LIR

> All phi functions at the beginning of a block have *parallel copy* semantics,
> i.e., they are not ordered. All phi functions together specify a permutation
> of registers and not a list of copies. Therefore, it would be
> counterproductive to assign individual operation numbers to them; we just
> attach them to the block label.

> The lifetime interval for the virtual register defined by a phi function
> starts directly at the beginning of the block.

I guess that makes sense since the phis are attached to the block label

> The lifetime intervals for the virtual registers used by a phi function end
> at the end of the corresponding predecessor blocks (as long as the virtual
> registers are not used by other operations after the phi function).

> Note that both with and without SSA form, no coalescing of non-overlapping
> lifetime intervals is performed. Without SSA form, i.e., in the current
> product version, it would be too slow and complicated. With SSA form, it is
> not allowed because it would violate SSA form.

Why does coalescing itervals violate SSA? Is SSA a property of the intervals
somehow?

> [cont'd from above] In both cases, *register hints* are used as a lightweight
> replacement. Intervals that should be assigned the same physical register are
> connected via a register hint. The linear scan allocator honors this hint if
> possible, but is still allowed to assign different registers.

I previously thought hints were for calling convention stuff but now after
reading this I don't know why we would use a hint. Maybe for things like
suggesting `this` be in the same register over the whole function?

> The source and target of a move are connected with such a hint. With SSA
> form, the input and result operands of a phi function are also connected.
> [...] In this small example, the register hints lead to machine code without
> any move instructions, both with and without SSA form.

Oh, it's to avoid data motion for optimization reasons. I guess linear scan can
get rid of moves within a block and between blocks.

## 4. Lifetime Analysis

> The linear scan algorithm does not operate on a structured control flow
> graph, but on a linear list of blocks. The block order has a high impact on
> the quality and speed of linear scan: A good block order leads to short
> lifetime intervals with few holes.

> Our block order guarantees the following properties: First, all predecessors
> of a block are located before this block, with the exception of backward
> edges of loops. This implies that all dominators of a block are located
> before this block.

Sounds like RPO

> Secondly, all blocks that are part of the same loop are contiguous, i.e.,
> there is no non-loop block between two loop blocks. Even though the current
> product version of the client compiler’s linear scan algorithm could operate
> on any block order, this order turned out to be best.

Sounds...harder. Maybe starting with RPO, you can find loop headers and use
those to group loop bodies somehow?

https://pages.cs.wisc.edu/~fischer/cs701.f14/finding.loops.html

### 4.1 Algorithm

> Input of the algorithm:
>
> 1. Intermediate representation in SSA form. An operation has
> input and output operands. Only virtual register operands are
> relevant for the algorithm.
> 2. A linear block order where all dominators of a block are before
> this block, and where all blocks belonging to the same loop are
> contiguous. All operations of all blocks are numbered using this
> order.

The input of the algorithm is defined to be this special ordering of blocks
with contiguous loop blocks even though they just said that linear scan could
operate on any block order. Maybe this is the bit that avoids dataflow analysis
for liveness?

> Output of the algorithm: One lifetime interval for each virtual register,
> covering operation numbers where this register is alive, and with lifetime
> holes in between. Thus, a lifetime interval consists of one or more *ranges*
> of operation numbers.

Wow, that last sentence is key. Grade school math told me that interval means a
pair of `(start, end)` but they call that a *range* and mean that an interval
is a list of those pairs. I missed this sentence on every previous (attempted)
reading of this paper.

> Figure 4 shows the algorithm. In addition to the input and output data
> structures, it requires a set of virtual registers, called *liveIn*, for each
> block.

It's not immediately clear to me, but it seems like this is not required to be
filled out before the algorithm starts, just that a bunch of different (empty)
sets be made available so that they can be filled in during the algorithm.

I think if one were to do normal dataflow analysis of liveness and *then* do
`BuildIntervals`, all writes to `live` and `liveIn` in Figure 4 are
unnecessary. Also, the special loop header handling goes away? Maybe?

> For each live register, an initial live range covering the entire block is
> added. This live range might be shortened later if the definition of the
> register is encountered.

> Next, all operations of b are processed in reverse order.
> 
> An output operand, i.e., a definition of a virtual register, shortens the
> current range of the register’s lifetime interval; the start position of the
> first range is set to the current operation.

The `intervals[opd].setFrom(op.id)` notation confused me before and this
sentence (alongside aforementioned definition of an interval) clears it up.
Since `intervals[opd]` is an array, the `setFrom` threw me off. It's really
more like `intervals[opd].findFirstRange().setFrom(op.id)`. Also, they use both
*start* and *from* to refer to the same thing.

But what is the first range? First ordered by what?

> An input operand, i.e., a use of a virtual register, adds a new range to the
> lifetime interval (the new range is merged if an overlapping range is
> present). The new live range starts at the beginning of the block, and again
> might be shortened later.

This took me a sec and some gesturing in the air with my hands but there's two
cases: either the input operand *v* is not defined in the block *b*, in which
case it's live-in to *b*, or it is defined in the block, in which case (because
of SSA and dominator property; there aren't going to be any uses before this
definition), the definition will shorten the current live range of *v*, which
is also necessarily the first range.

I don't think it's expressly mentioned in the paper, but I guess all ranges in
an interval should be disjoint?

> Because the live range of a phi function starts at the beginning of the
> block, it is not necessary to shorten the range for its output operand. The
> operand is only removed from the set of live registers. The input operands of
> the phi function are not handled here, because this is done independently
> when the different predecessors are processed. Thus, neither an input operand
> nor the output operand of a phi function is live at the beginning of the phi
> function’s block.

It feels really weird that inputs to a phi in block *b* are not live-in to *b*.
I wonder why it's handled separately like this instead of the phi handling at
the top of `BuildIntervals`. Maybe some weird interaction with the loop header
special handling?

> When a loop’s end block is processed, the loop header has not been processed,
> so its *liveIn* set is still empty. Therefore, registers that are alive for the
> entire loop are missing at this time. These registers are known at the time
> the loop header is processed: All registers live at the beginning of the loop
> header must be live for the entire loop, because they are defined before the
> loop and used inside or after it. Using the property that all blocks of a
> loop are contiguous in the linear block order, it is sufficient to add one
> live range, spanning the entire loop, for each register that is live at the
> beginning of the loop header.

Yeah I am still wondering if we can avoid this with dataflow. I think so. This
is probably much faster if you already know loop headers.

## 5. Linear Scan Algorithm

> The main linear scan algorithm needs no modifications to work on SSA form.

That's neat.

> Because the algorithm is extensively described in [30], we give only a short
> summary here.

([30] is *Optimized Interval Splitting in a Linear Scan Register Allocator* by
Wimmer and Mössenböck.)

Sigh. I am trying to read this paper to understand linear scan, after all.

> It processes the lifetime intervals sorted by their start position and
> assigns a register or stack slot to each interval.

I guess since intervals and virtual registers are 1:1 this makes sense, though
potentially pins registers for a long time (remember the interval holes!).
Maybe this is what the interval splitting is for? So an original interval *i*
can be split into *i0* and *i1* and each of *i0* and *i1* could be assigned
different physical locations as needed?

> For this, four sets of intervals are managed:
> 
> *unhandled* contains the intervals that start after the current position and
> are therefore not yet of interest;
> 
> *active* contains the intervals that are live at the current position;
> 
> *inactive* contains the intervals that start before and end after the current
> position, but that have a lifetime hole at the current position; and
> 
> *handled* contains the intervals that end before the current position and are
> therefore no longer of interest.

> An interval can switch several times between *active* and *inactive* until it
> is finally moved to *handled*. If a register is not available for the entire
> lifetime of an interval, this or another interval is split and spilled to a
> stack slot, leading to new intervals added to the *unhandled* set during the
> run of the algorithm. However, the algorithm never backtracks, i.e., all added
> intervals always start after the current position.

I need a state diagram.

> The main part of the linear scan algorithm is the selection of a free
> register if one is available, or the selection of an interval to be split and
> spilled if no register is available.

> While the original linear scan algorithm [22] was designed to have linear
> runtime complexity, the extensions to support lifetime holes and interval
> splitting [28, 30] introduced non-linear parts. [...] The [*inactive*] set
> can contain an arbitrary number of intervals since it is not bound by the
> number of physical registers. Testing the current interval for intersection
> with all of them can therefore be expensive.

([22] is the original *Linear scan register allocation* by Poletto and Sarkar
and [28] is *Quality and Speed in Linear-scan Register Allocation* by Traub,
Holloway, and Smith. I hadn't realize that this Linear Scan on SSA paper
subsumed [28] too. So really the prerequisite reading/understanding for the
current paper is [22], [28], [30].)

> When the lifetime intervals are created from code in SSA form, this test is
> not necessary anymore: All intervals in *inactive* start before the current
> interval, so they do not intersect with the current interval at their
> definition. They are inactive and thus have a lifetime
> hole at the current position, so they do not intersect with the current
> interval at its definition. SSA form therefore guarantees that they
> never intersect [7], making the entire loop that tests for intersection
> unnecessary.

([7] is *Fast copy coalescing and live-range identification* by Budimlic,
Cooper, Harvey, Kennedy, Oberg, and Reeves.)

Well that's really cool!

> Unfortunately, splitting of intervals leads to intervals that no
> longer adhere to the SSA form properties because it destroys SSA
> form. Therefore, the intersection test cannot be omitted completely;
> it must be performed if the current interval has been split off from
> another interval.

Oh, darn.

> In summary, the highlighted parts of Figure 6 can be guarded by a check
> whether *current* is the result of an interval split, and need not be executed
> otherwise. For our set of Java benchmarks, this still saves 59% to 79% of all
> intersection tests.

Oh, nice.

Anyway, this is where we have to go read the previous Wimmer paper from 2005
([30]) because it explains the splitting and whatnot in detail. Figure 2,
Figure 4, and Figure 5 look helpful. TODO(max): Break it down.

I wish they had included a figure in this current paper for their modified new
algorithms. Several times in this paper they vaguely handwave that "a
modification" is needed but don't say what, and as your local idiot, I find
this really grating. *Even if* the modification to `TryAllocateFreeReg` and
`AllocateBlockedReg` is really just a guard on the intersection tests, it would
be nice to see reified in a figure.

## 6. Resolution and SSA Form Deconstruction

> Because the control flow graph is reduced to a list of blocks, control flow
> is possible between blocks that are not adjacent in the list. When the
> location of an interval is different at the end of the predecessor and at the
> start of the successor, a move instruction must be inserted to resolve the
> conflict.

> The resolving moves for a control flow edge have the same semantics as the
> moves necessary to resolve phi functions: They must be treated as parallel
> copies, i.e., a mapping from source to target locations. The only difference
> is that moves resulting from interval splitting originate from a single
> interval, while moves resulting from phi functions have different intervals
> for the source and the target.

Why is this significant? What does it matter that they originate from a single
interval?

> Adding SSA form deconstruction requires only small extensions to the existing
> resolution algorithm. Figure 7 shows the entire algorithm.

Figure 7 shows that we're using live-in sets despite the earlier part of the
paper saying that they are temporary and we can throw them away? The
previous live-in sets dealt in virtual registers, not intervals, so maybe this
is a new map entirely. Where does it come from?

> Because all moves must be ordered properly, they are first added to a mapping
> and then ordered and inserted afterwards. This part of the algorithm is not
> shown because it requires no SSA form specific changes.

Aargh! How am I supposed to figure out what `orderAndInsertMoves` does then?
It's not even described, not really. Is this the windmill stuff? So much for
"the entire algorithm".

> Both the source and target operand of a move can be a stack
> slot. Because one interval is assigned only one stack slot even when
> it is split and spilled multiple times, moves between two different
> stack slots can only occur with our added handling for phi functions.

> Stack-to-stack moves are not supported by most architectures
> and must be emulated with either a load and a store to a register
> currently not in use, or a push and a pop of a memory location if
> no register is free.

> Our implementation for the Intel x86 architec-
> ture does not reserve a scratch register that is always available for
> such moves. However, the register allocator has exact knowledge if
> there is a register that is currently unused, and it is also possible to
> use a floating point register for an integer value because no com-
> putations need to be performed. Therefore, a register is available in
> nearly all cases.

> Still, a stack-to-stack moves requires two machine
> instructions, so we try to assign the same stack slot to the source
> and target of a phi function when the according intervals do not
> overlap.

## 7. Evaluation

> The lifetime analysis is 25% to 31% faster because the algorithm
> described in Section 4 needs no global data flow analysis.

Ok, not a *terrible* hit if we do dataflow, given that lifetime analysis looks
like it is between 25 and 30% of the total regalloc time.

## 8. Related Work

[19] They use data flow analysis to
construct the lifetime intervals, pre-order the moves and phi func-
tions instead of using the parallel copy semantics of the phi func-
tions, and use no structural properties guaranteed by SSA form.

Boissinot et al. present a fast algorithm for liveness check-
ing of SSA form programs, using the structural properties guar-
anteed by SSA form [4]. Their algorithm performs only few pre-
computations, but still allows fast answers to the question whether a
certain value is live at a certain point in a method. It is not designed
to allow fast answers for all points in the program, therefore it is
not suitable for building lifetime intervals. Our algorithm to build
lifetime intervals requires more time than their pre-computation,
but then the intervals contain information about the lifetime of all
values for the entire method.

Boissinot

## Other notes

Is this different for basic block arguments? IIRC basic block arguments handle
at least one case that phi cannot, which is block B1 jumping to B2 twice with
different arguments. But maybe this is fine if edges are addressed differently
than (B1, B2)? Phi operands are keyed on input edges and must be unique

Look at https://arxiv.org/abs/2011.05608 and
https://mastodon.social/@tekknolagi/113326337405036375 --- Ian recommends
allocating in reverse (which also helps avoid keeping phis alive until the
beginning of the function?)

C1 Linear scan (~7k lines):

https://github.com/openjdk/jdk/blob/a23de2ec090628b52532ee5d9bd4364a97499f5b/src/hotspot/share/c1/c1_LinearScan.hpp
https://github.com/openjdk/jdk/blob/a23de2ec090628b52532ee5d9bd4364a97499f5b/src/hotspot/share/c1/c1_LinearScan.cpp

C1 Linear scan phases:

1. number all instructions in all blocks
2. compute local live sets separately for each block (sets live_gen and
   live_kill for each block)
3. perform a backward dataflow analysis to compute global live sets
   (sets live_in and live_out for each block)
4. build intervals
5. actual register allocation
6. resolve data flow (insert moves at edges between blocks if intervals have
   been split)
7. assign register numbers back to LIR
8. verify (check that all intervals have a correct register and that no registers are overwritten)

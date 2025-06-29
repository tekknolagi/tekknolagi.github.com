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

> All phi functions at the beginning of a block have parallel copy semantics,
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

## Other notes

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

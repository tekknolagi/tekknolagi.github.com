---
title: "Linear scan register allocation in Datalog"
layout: post
co_authors: Yannis Smaragdakis
---

Last year I wrote [my first register allocator](/blog/linear-scan): a small
linear scan implementation in Ruby on SSA. Immediately after publishing the
post, I had a call with [Waleed Khan](https://waleedkhan.name) where he walked
me through implementing the liveness fixpoint algorithm in Datalog. We talked
about doing the whole register allocator in Datalog---how hard could it
be?---but it never materialized.

This year, though, I met [Yannis Smaragdakis](https://yanniss.github.io/) at
PLDI. I did my usual thing, which is to nerd snipe other people into doing
interesting projects: in this case, full linear scan in Datalog. Several short
hours and several beers later, we have an implementation that I only mostly
understand.

## Encoding

As with Waleed, we started off with the Wimmer2010 CFG example from the last
two posts.

<!--
# dot IN.dot -Tsvg -Nfontname=Monospace -Efontname=Monospace > OUT.svg

digraph G {
node [shape=plaintext]
B1 [label=<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0">
<TR><TD PORT="params" BGCOLOR="lightgray">B1(0:R10, 1:R11)&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">2:jump →B2($1, R11)&nbsp;</TD></TR>
</TABLE>>];
B1:s -> B2:params:n;
B2 [label=<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0">
<TR><TD PORT="params" BGCOLOR="lightgray">B2(3:R12, 4:R13)&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">5:cmp R13, $1&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="1">6:blt →B4, →B3&nbsp;</TD></TR>
</TABLE>>];
B2:s -> B4:params:n;
B2:s -> B3:params:n;
B3 [label=<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0">
<TR><TD PORT="params" BGCOLOR="lightgray">7:B3()&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">9:R14 = 8:mul R12, R13&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="1">11:R15 = 10:sub R13, $1&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="2">12:jump →B2(R14, R15)&nbsp;</TD></TR>
</TABLE>>];
B3:s -> B2:params:n;
B4 [label=<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0">
<TR><TD PORT="params" BGCOLOR="lightgray">13:B4()&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">15:R16 = 14:add R10, R12&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="1">16:ret R16&nbsp;</TD></TR>
</TABLE>>];
}
-->

<figure>
<object class="svg" type="image/svg+xml" data="/assets/img/wimmer-lsra-cfg-numbered.svg"></object>
<figcaption markdown=1>
The graph from Wimmer2010 has come back! Again! Remember, we're using block
arguments instead of phis, so `B1(R10, R11)` defines R10 and R11 before the
first instruction in B1.

Additionally, we have numbered the instructions in the graph. For each
instruction that defines a value, we have also given it two separate numbers:
one number N at which it uses its inputs and one number N+1 at which it defines
its output.

We do this so that the logic can process one event at a time: the end of a
liveness interval happens before the need to possibly allocate a register
because a variable gets assigned. This is visible in labels 8 and 9 as well as
14 and 15.

We have similarly modified block parameters: we sequentialize assignment by
picking different instruction indices for each block parameter in a block. This
is visible in labels 0 and 1.
</figcaption>
</figure>

This time, however, we encoded the program in terms of instructions instead of
in terms of blocks: we modeled which instructions used which registers and
def-ed which registers.

One difference from the original paper and also the blog post, which will be
explained later, is that we modeled each block parameter as having its own
instruction index. This difference only appears in phis/block parameters: every
other instruction only defines one SSA value at an index.

Overall this is an excellent demonstration of programming in Datalog. It shows
tasks that Datalog handles very elegantly, such as the basic block computation
and the liveness analysis, which are just a handful of lines.

At the same time, it shows how to do things that are complex to do in Datalog,
namely the linear scan algorithm. The algorithm is very sequential,
imperative-style. It has steps such as "remove an interval that expires",
"update the stack of intervals to evict the one ending last", etc. All of these
operations have to be simulated declaratively.

The final result contains perhaps the most advanced Datalog programming
pattern, a forall emulation: iterating declaratively over a pre-determined set
(in this case, to select the interval corresponding to the variable to spill).

## Liveness

Since we complicated our input data a bit (variables are no longer def-ed in
blocks but instead def-ed in their own instruction indices), we had to expand
out liveness. Now we do a lot more:

* compute block boundaries
* compute what instructions are in what block
* compute the control-flow graph structure
* compute live-in and live-out for each instruction (not block!)
* and finally, compute liveness intervals

Still, liveness is the most straightforward part of the implementation. There
are 4 rules defining `live_out` and `live_in` in mutual recursion. These
correspond to the standard data-flow equations in textbook definitions of
liveness. (The transfer function is encoded as two rules for explicitness, to
capture that a variable is live-in if it is used by the instruction, or if it
was live-out and is not def-ed.)

```
live_out(insn, var) :-
  block_succ(prev, succ),
  block_head(succ, headinsn),
  live_in(headinsn, var),
  block_tail(prev, insn).

live_in(insn, var) :-
  var_use(insn, var).

live_in(insn, var) :-
  live_out(insn, var),
  !var_def(insn, var).

live_out(insn, var) :-
  live_in(next, var),
  next_in_block(insn, next).
```


```
// Liveness starts right after start, ends right before end
.decl liveness_interval(start:Instruction, end:Instruction, var:Var)

liveness_interval(start, end, var) :-
  live_out(_, var),
  start = min b : { live_out(b, var) },
  end = max b : { live_in(b, var) }.
```

## Spill decisions

## Free sets

## Multisets and stratification

## Extensions

[Implementation](https://gist.github.com/yanniss/c04e75faf8dc395a5055cdabe7c6e3d4)

<!--
.type Block <: number
.type Var <: symbol
.type Instruction <: number

// Input
.decl var_use(insn:Instruction, var:Var)
.decl var_def(insn:Instruction, var:Var)

.decl jump(insn:Instruction, next:Instruction)
.decl jumpi(insn:Instruction, next:Instruction, otherNext:Instruction)
.decl ret(insn:Instruction)

#define NUM_REGS 2

// Facts: maintain the property that defs use a unique instruction so that liveness intervals end
// before the def
var_def(0, "R10").
var_def(1, "R11").

var_use(2, "R11").

var_def(3, "R12").
var_def(4, "R13").

var_use(5, "R13").

var_use(8, "R12").
var_use(8, "R13").
var_def(9, "R14").

var_use(10, "R13").
var_def(11, "R15").

var_use(12, "R14").
var_use(12, "R15").

var_use(14, "R10").
var_use(14, "R12").
var_def(15, "R16").

var_use(16, "R16").

.decl min_instruction(insn:Instruction)
min_instruction(-1).

.decl max_instruction(insn:Instruction)
max_instruction(16).

jump(2, 3).
jumpi(6, 7, 13).
jump(12, 3).
ret(16).

.type Register <: number


// Scaffolding
.decl is_register(reg:Register)
is_register(0).
is_register(i+1) :- is_register(i), i < NUM_REGS-1.

.decl is_var(var:Var)
is_var(var) :-
  var_def(_, var); var_use(_, var).

.decl is_none_var(var:Var)
is_var(var),
is_none_var(var) :-
  is_register(reg),
  var = cat("none",to_string(reg)).

// Arbitrary total order on vars
.decl higher_var(var:Var, higher:Var)
higher_var(var, higher) :-
  is_var(var),
  is_var(higher),
  ord(var) < ord(higher).

// Basic blocks
.decl instruction_block(insn:Instruction, block:Block)
instruction_block(0, as(0, Block)).

instruction_block(head, as(head, Block)) :-
  jump(_, head);
  jumpi(_, head, _);
  jumpi(_, _, head).

instruction_block(prev + 1, head) :-
  instruction_block(prev, head),
  !jump(prev, _),
  !jumpi(prev, _, _),
  !ret(prev),
  prev < maxi,
  max_instruction(maxi).

.decl block_head(block:Block, insn:Instruction)
block_head(block, as(block, Instruction)) :-
  instruction_block(_, block).

.decl block_tail(block:Block, insn:Instruction)
block_tail(block, insn) :-
  instruction_block(insn, block),
  (jump(insn, _);
   jumpi(insn, _, _);
   ret(insn)).

.decl next_in_block(insn:Instruction, next:Instruction)
next_in_block(insn, insn+1) :-
  instruction_block(insn, block),
  instruction_block(insn+1, block).

.decl block_succ(prev:Block, next:Block)
block_succ(prev, next) :-
  (jump(insn, nextInsn);
   jumpi(insn, nextInsn, _);
   jumpi(insn, _, nextInsn)),
  instruction_block(insn, prev),
  instruction_block(nextInsn, next).

.decl live_out(insn:Instruction, var:Var)
.decl live_in(insn:Instruction, var:Var)

live_out(insn, var) :-
  block_succ(prev, succ),
  block_head(succ, headinsn),
  live_in(headinsn, var),
  block_tail(prev, insn).

live_in(insn, var) :-
  var_use(insn, var).

live_in(insn, var) :-
  live_out(insn, var),
  !var_def(insn, var).

live_out(insn, var) :-
  live_in(next, var),
  next_in_block(insn, next).

// Liveness starts right after start, ends right before end
.decl liveness_interval(start:Instruction, end:Instruction, var:Var)
liveness_interval(start, end, var) :-
  live_out(_, var),
  start = min b : { live_out(b, var) },
  end = max b : { live_in(b, var) }.

.output liveness_interval

// after instruction, this is the register assignment. Var = "none" is a possibility
.decl reg_assignment(insn:Instruction, reg:Register, var:Var)
.decl spilled_var(insn:Instruction, var:Var)

// Initially all registers are free
reg_assignment(minInsn, reg, cat("none",to_string(reg))) :-
  min_instruction(minInsn),
  is_register(reg).

// Assignments are propagated if there is no def, no liveness expiration
reg_assignment(insn+1, reg, var) :-
  reg_assignment(insn, reg, var),
  !var_def(insn, _),
  !liveness_interval(_, insn, var),
  !max_instruction(insn).

// Register gets freed if there is liveness expiration. This cannot coincide with def.
reg_assignment(insn+1, reg, cat("none",to_string(reg))) :-
  reg_assignment(insn, reg, var),
  liveness_interval(_, insn, var),
  !max_instruction(insn).

// If there is a def, consider spilling, if there's a better victim than the def-ed var
spilled_var(insn+1, var),
reg_assignment(insn+1, reg, defedVar) :-
  all_reg_best_victim(insn, var),
  reg_assignment(insn, reg, var),
  var_def(insn, defedVar),
  !max_instruction(insn).

// The def-ed var is the best victim, keep the register assignment the same
reg_assignment(insn+1, reg, var) :-
  all_reg_best_victim(insn, defedVar),
  reg_assignment(insn, reg, var),
  var_def(insn, defedVar),
  !max_instruction(insn).

// The best victim is in a register, other regs than the best victim are just copied
reg_assignment(insn+1, otherReg, otherVar) :-
  all_reg_best_victim(insn, var),
  reg_assignment(insn, reg, var),
  reg_assignment(insn, otherReg, otherVar),
  reg != otherReg,
  !max_instruction(insn).

.output reg_assignment

// Forall emulation to find the best victim by iterating over all registers
.decl all_reg_best_victim(insn:Instruction, victimVar:Var)
all_reg_best_victim(insn, victimVar) :-
  best_victim_up_to_reg(NUM_REGS-1, insn, victimVar).

.decl best_victim_up_to_reg(upToReg:Register, insn:Instruction, victimVar:Var)
best_victim_up_to_reg(0, insn, victimVar) :-
  var_def(insn, defedVar),
  reg_assignment(insn, 0, firstVar),
  better_victim_var(firstVar, defedVar, victimVar).

best_victim_up_to_reg(reg+1, insn, victimVar) :-
  best_victim_up_to_reg(reg, insn, prevVictimVar),
  reg_assignment(insn, reg+1, nextVar),
  better_victim_var(nextVar, prevVictimVar, victimVar),
  reg < NUM_REGS-1.

// General comparator between variables, to decide on a victim to spill.
.decl better_victim_var(var:Var, otherVar:Var, victimVar:Var)
// if one slot is empty (none var) and the other is not, then the empty one is a better victim
better_victim_var(var, otherVar, var),
better_victim_var(otherVar, var, var) :-
  is_none_var(var),
  is_var(otherVar),
  !is_none_var(otherVar).

// arbitrary ordering
better_victim_var(var, otherVar, var),
better_victim_var(otherVar, var, var) :-
  is_none_var(var),
  is_none_var(otherVar),
  higher_var(var, otherVar).

better_victim_var(var, otherVar, var),
better_victim_var(otherVar, var, var) :-
  liveness_interval(start1, end1, var),
  liveness_interval(start2, end2, otherVar),
  !is_none_var(var), // unnecessary?
  !is_none_var(otherVar),  // unnecessary?
  (end1 > end2;
   (end1 = end2, start1 > start2)).

/////// Final output
.decl assignment(reg:Register, var:Var)
assignment(reg, var) :-
  reg_assignment(_, reg, var),
  !spilled_var(_, var).

.output assignment
-->

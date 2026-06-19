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
because a variable gets assigned. (This is visible in labels 8,9 as well as
14,15.)
</figcaption>
</figure>

This time, however, we encoded the program in terms of instructions instead of
in terms of blocks: we modeled which instructions used which registers and
def-ed which registers.

One difference from the original paper and also the blog post, which will be
explained later, is that we modeled each block parameter as having its own
instruction index. This difference only appears in phis/block parameters: every
other instruction only defines one SSA value at an index.

## Liveness

## Spill decisions

## Free sets

## Multisets and stratification

## Extensions

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

<figure>
<object class="svg" type="image/svg+xml" data="/assets/img/wimmer-lsra-cfg.svg"></object>
<figcaption markdown=1>
The graph from Wimmer2010 has come back! Again! Remember, we're using block
arguments instead of phis, so `B1(R10, R11)` defines R10 and R11 before the
first instruction in B1.
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

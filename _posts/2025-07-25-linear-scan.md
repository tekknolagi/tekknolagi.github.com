---
title: "Linear scan register allocation on SSA"
layout: post
---

How do JIT compilers do register allocation? Well, "everyone knows" that
"every JIT does its own variant of linear scan". This bothered me for some time
because I've worked on a couple of JITs and still didn't understand the backend
bits.

I also didn't realize that there were more than one or two papers on linear
scan. So this post will serve as a bit of a survey or a history of linear
scan---as best as I can figure it out, anyway. If you were in or near the room
where it happened, please feel free to reach out and correct some parts.

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
post we'll focus on *linear scan*.

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
registers. A live range is a pair of [start, end) (end is exclusive) that
begins when the register is defined and ends when it is last used. In
non-SSA-land, these live ranges are different from the virtual registers: they
represent some kind of lifetimes of each *version* of a virtual register. For
an example, consider the following assembly-like language snippet with virtual
registers (defined by `... -> destination`):


```
... -> a
1 + a -> b
1 + b -> c
1 + c -> a
1 + a -> d
```

There are two definitions of `a` and they each live for different amounts of
time:

```
               a  b  c  a  d
...   -> a     |
1 + a -> b     v  |
1 + b -> c        v  |
1 + c -> a           v  |
1 + a -> d              v  |
```

In fact, the intervals are completely disjoint. It wouldn't make sense for the
register allocator to consider variables, because there's no reason the two
`a`s should necessarily live in the same physical register.

In SSA land, it's a little different: since each virtual registers only has one
definition (by, uh, definition), live ranges are an exact 1:1 mapping with
virtual registers. **We'll focus on SSA for the remainder of the post because
this is what I am currently interested in.** The research community seems to
have decided that allocating directly on SSA gives more information to the
register allocator.

Linear scan starts at the point in your compiler process where you already know
how these live ranges---that you have already done some kind of analysis to
build a mapping.

Part of this analysis is called *liveness analysis*. The result of liveness
analysis is a mapping of `BasicBlock -> Set[Instruction]` that tells you which
virtual registers (remember, since we're in SSA, instruction==vreg) are alive
(used later) at the beginning of the basic block. This is called a *live-in*
set. For example:

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
it as an exercise.

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
<figcaption>Working backwards from each of A and B,</figcaption>
</figure>

That is, if there were some register R0 live-in to B and some register R1
live-in to A, both R0 and R1 would be live-out of C. They may also be live-in
to C, but that entirely depends on the contents of C.










In order to build live ranges, you have to have some kind of numbering system
for your instructions, otherwise a live range's start and end are meaningless.
We can write a function that fixes a particular block order (in this case,
reverse post-order) and then assigns each block and instruction a number in a
linear sequence. You can think of this as flattening or projecting the graph:

```ruby
class Function
  def number_instructions!
    @block_order = rpo
    number = 16
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







The liveness analysis tells you which basic
blocks need which virtual registers to be alive on entry. This is a
*graph-land* notion: it operates on your control-flow graph which has not yet
been assigned an order.

Consider the following assembly-like language snippet with virtual registers
(defined by `... -> destination`):

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

<!--
digraph G {
node [shape=plaintext]
B1 [label=<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0">
<TR><TD PORT="params" BGCOLOR="gray">B1(V10, V11)&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">jump →B2($1, V11)&nbsp;</TD></TR>
</TABLE>>];
B1:0 -> B2:params;
B2 [label=<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0">
<TR><TD PORT="params" BGCOLOR="gray">B2(V12, V13)&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">cmp V13, $1&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="1">blt →B4, →B3&nbsp;</TD></TR>
</TABLE>>];
B2:1 -> B4:params;
B2:1 -> B3:params;
B3 [label=<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0">
<TR><TD PORT="params" BGCOLOR="gray">B3()&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">V14 = mul V12, V13&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="1">V15 = sub V13, $1&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="2">jump →B2(V14, V15)&nbsp;</TD></TR>
</TABLE>>];
B3:2 -> B2:params;
B4 [label=<<TABLE BORDER="0" CELLBORDER="1" CELLSPACING="0">
<TR><TD PORT="params" BGCOLOR="gray">B4()&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="0">V16 = add V10, V12&nbsp;</TD></TR>
<TR><TD ALIGN="left" PORT="1">ret V16&nbsp;</TD></TR>
</TABLE>>];
}
-->
<figure>
<object class="svg" type="image/svg+xml" data="/assets/img/wimmer-lsra-cfg.svg"></object>
<figcaption>
blah
</figcaption>
</figure>

```
label Bentry:
R10 = ...
R11 = ...
    jmp B2(1, R11)
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

TODO insert a diagram

```ruby
class Function
  def compute_initial_liveness_sets order
    gen = Hash.new 0
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
    # Map from Block to bitset of VRegs live at entry
    order = post_order
    gen, kill = compute_initial_liveness_sets(order)
    live_in = Hash.new 0
    changed = true
    while changed
      changed = false
      for block in order
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

The interval construction tells you where a virtual register is first defined
and where it is last used.


After liveness analysis, you need to build intervals.

Implicit here is that you have already *scheduled* your instructions. So you
need to do that too.

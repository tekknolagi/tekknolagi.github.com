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
Holloway, and Smith. It adds some optimizations to the algorithm presented in
the tcc paper.

The first paper I read, and I think the paper everyone refers to when they talk
about linear scan, is [Linear Scan Register
Allocation](/assets/img/linearscan-ra.pdf) (PDF, 1999) by Poletto and Sarkar.
In this paper, they give a fast alternative to graph coloring register
allocation, especially motivated by just-in-time compilers. In retrospect, it
seems to be a bit of a rehash of the previous two papers.

Linear scan starts at the point in your compiler process where you already know
how long each virtual register needs to live---that you have already done some
kind of *liveness analysis* . The liveness analysis tells you which basic
blocks need which virtual registers to be alive on entry. This is a
*graph-land* notion: it operates on your control-flow graph which has not yet
been assigned an order.

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

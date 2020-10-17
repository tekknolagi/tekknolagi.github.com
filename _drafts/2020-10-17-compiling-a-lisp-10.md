---
title: "Compiling a Lisp: Instruction encoding interlude"
layout: post
date: 2020-10-17 11:00:06 PDT
description: In which we learn a little about x86-64 instruction encoding
---

Welcome back to the Compiling a Lisp series. In this thrilling new update, we
will learn a little bit more about x86-64 instruction encoding instead of
allocating more interesting things on the heap or adding procedure calls.

I am writing this interlude because I changed one register in my compiler code
(`kRbp` to `kRsp`) and all hell broke loose --- the resulting program was
crashing, `rasm2`/Cutter were decoding wacky instructions when fed my binary,
etc. Over the span of two very interesting but very frustrating hours, I
learned why I had these problems and how to resolve them. You should learn,
too.

### State of the instruction encoder

Recall that I introduced at least 10 functions that looked vaguely like this:

```c
void Emit_mov_reg_imm32(Buffer *buf, Register dst, int32_t src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0xc7);
  Buffer_write8(buf, 0xc0 + dst);
  Buffer_write32(buf, src);
}
```

These functions all purport to encode x86-64 instructions. They do, most of the
time, but they do not tell the whole story. This function is supposed to encode
an instruction of the form `mov reg64, imm32`. How does it do it? I don't know!

They have all these magic numbers in them! What is a `kRexPrefix`? Well, it's
`0x48`. Does that mean anything to us? No! It gets worse. What are `0xc7` and
`0xc0` doing there? Why are we adding `dst` to `0xc0`? Before this debugging
and reading extravaganza, I could not have told you. Remember how somewhere in
a previous post I mentioned I was getting these hex bytes from reading the
compiled output on the Compiler Explorer? Yeah.

As it turns out, this is not a robust development strategy, at least with
x86-64. It might be okay for some more regular or predictable instruction sets,
but not this one.

### Big scary documentation

So where do we go from here? How do we find out how to take these mystical
hexes and incantations to something that better maps to the hardware? Well, we
once again drag [Tom](https://tchebb.me/)[^1] into a debugging session and pull
out the big ol' Intel [Software Developer Manual][manual].

[manual]: https://software.intel.com/content/www/us/en/develop/articles/intel-sdm.html

This is an enormous 26MB, 5000 page manual comprised of four volumes. It's
*very intimidating*. This is exactly why I didn't want to pull it out earlier
and do this properly from the beginning... but here we are, eventually needing
to do it properly.

I will not pretend to understand all of this manual, nor will this post be a
guide to the manual. I will just explain what sections and diagrams I found
useful in understanding how this stuff works.

I only ever opened Volume 2, the instruction set reference. In that honking
2300 page volume are descriptions of every Intel x86-64 instruction and how
they are encoded. The instructions are listed alphabetically and split into
sections based on the first letter of each instruction name.

Let's take a look at Chapter 3, specifically at the MOV instruction on page
1209. For those following along who do not want to download a massive PDF, this
[website](https://www.felixcloutier.com/x86/index.html) has a bunch of the same
data in HTML form. Here's the [page for
MOV](https://www.felixcloutier.com/x86/mov).

This page has every variant of MOV instruction. There are other instructions
begin with MOV, like MOVAPD, MOVAPS, etc, but they are different enough that
they are different instructions.

It has six columns:

* *Opcode*, which describes the layout of the bytes in the instruction stream.
  This describes how we'll encode instructions.
* *Instruction*, which gives a text-assembly-esque representation of the
  instruction. This is useful for figuring out which one we actually want to
  encode.
* *Op/En*, which stands for "Operand Encoding" and as far as I can tell
  describes the operand order with a symbol that is explained further in the
  "Instruction Operand Encoding" table on the following page.
* *64-Bit Mode*, which tells you if the instruction can be used in 64-bit mode
  ("Valid") or not (something else, I guess).
* *Compat/Leg Mode*, which tells you if the instruction can be used in some
  other mode, which I imagine is 32-bit mode or 16-bit mode. I don't know. But
  it's not relevant for us.
* *Description*, which provides a "plain English" description of the opcode,
  for some definition of the words "plain" and "English".

Other instructions have slightly different table layouts, so you'll have to
work out what the other columns mean.

Here's a preview of some rows from the table, with HTML courtesy of Felix
Cloutier's aforementioned web docs:

<table>
<tr>
<th>Opcode</th>
<th>Instruction</th>
<th>Op/En</th>
<th>64-Bit Mode</th>
<th>Compat/Leg Mode</th>
<th>Description</th></tr>
<tr>
<td>88 /<em>r</em></td>
<td>MOV <em>r/m8,r8</em></td>
<td>MR</td>
<td>Valid</td>
<td>Valid</td>
<td>Move <em>r8</em> to <em>r/m8.</em></td></tr>
<tr>
<td>REX + 88 /<em>r</em></td>
<td>MOV <em>r/m8</em><sup>***,</sup><em>r8</em><sup>***</sup></td>
<td>MR</td>
<td>Valid</td>
<td>N.E.</td>
<td>Move <em>r8</em> to <em>r/m8.</em></td></tr>
<tr>
<td>89 /<em>r</em></td>
<td>MOV <em>r/m16,r16</em></td>
<td>MR</td>
<td>Valid</td>
<td>Valid</td>
<td>Move <em>r16</em> to <em>r/m16.</em></td></tr>
<tr>
<td>89 /<em>r</em></td>
<td>MOV <em>r/m32,r32</em></td>
<td>MR</td>
<td>Valid</td>
<td>Valid</td>
<td>Move <em>r32</em> to <em>r/m32.</em></td></tr>
<tr>
<td>REX.W + 89 /<em>r</em></td>
<td>MOV <em>r/m64,r64</em></td>
<td>MR</td>
<td>Valid</td>
<td>N.E.</td>
<td>Move <em>r64</em> to <em>r/m64.</em></td></tr>
<tr>
<td>8A /<em>r</em></td>
<td>MOV <em>r8,r/m8</em></td>
<td>RM</td>
<td>Valid</td>
<td>Valid</td>
<td>Move <em>r/m8</em> to <em>r8.</em></td></tr>
<tr>
<td>REX + 8A /<em>r</em></td>
<td>MOV <em>r8***,r/m8***</em></td>
<td>RM</td>
<td>Valid</td>
<td>N.E.</td>
<td>Move <em>r/m8</em> to <em>r8.</em></td></tr>
<tr><td>...</td><td>...</td><td>...</td><td>...</td><td>...</td><td>...</td></tr>
</table>

<br />
<hr style="width: 100px;" />
<!-- Footnotes -->

[^1]: If you are an avid reader of this blog (Do those people exist? Please
      reach out to me. I would love to chat.), you may notice that Tom gets
      pulled into shenanigans a lot. This is because Tom is the best debugger I
      have ever encountered, he's good at reverse engineering, and he knows a
      lot about low-level things. I think right now he's working on improving
      open-source tooling for a RISC-V board for fun. But also he's very kind
      and helpful and generally interested in whatever ridiculous situation
      I've gotten myself into. Maybe I should add a list of the Tom Chronicles
      somewhere on this website. Anyway, everyone needs a Tom.

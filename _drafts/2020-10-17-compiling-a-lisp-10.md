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
<tr><td>...</td><td>...</td><td>...</td><td>...</td><td>...</td><td>...</td></tr>
<tr>
<td>C7 /<em>0 id</em></td>
<td>MOV <em>r/m32, imm32</em></td>
<td>MI</td>
<td>Valid</td>
<td>Valid</td>
<td>Move <em>imm32</em> to <em>r/m32.</em></td></tr>
<tr>
<td>REX.W + C7 /<em>0 id</em></td>
<td>MOV <em>r/m64, imm32</em></td>
<td>MI</td>
<td>Valid</td>
<td>N.E.</td>
<td>Move <em>imm32 sign extended to 64-bits</em> to <em>r/m64.</em></td></tr>
</table>

If you take a look at the last entry in the table, you'll see `REX.W + C7 /0
id`. Does that look familiar? Maybe, if you squint a little?

It turns out, that's the description for encoding the instruction we originally
wanted, and had a bad encoder for. Let's try and figure out how to use this to
make our encoder better. In order to do that, we'll need to first understand a
general layout for Intel instructions.

### Instruction encoding, big picture

All Intel x86-64 instructions follow this general format:

* *optional* instruction prefix (1 byte)
* opcode (1, 2, or 3 bytes)
* *if required,* Mod-Reg/Opcode-R/M, also known as ModR/M (1 byte)
* *if required,* Scale-Index-Base, also known as SIB (1 byte)
* displacement (1, 2, or 4 bytes, or none)
* immediate data (1, 2, or 4 bytes, or none)

I found this information at the very beginning of Volume 2, Chapter 2 (page
527) in a section called "Instruction format for protected mode, real-address
mode, and virtual-8086 mode".

You, like me, may be wondering about the difference between "optional", "if
required", and "..., or none". I have no explanation, sorry.

I'm going to briefly explain each component here, followed up with a
piece-by-piece dissection of the particular MOV instruction we want, so we get
some hands-on practice.

#### Instruction prefixes

There are a couple kind of instruction prefixes, like REX (Section 2.2.1) and
VEX (Section 2.3). We're going to focus on REX prefixes, since they are needed
for many (most?) x86-64 instructions, and we're not emitting vector
instructions.

The REX prefixes are used to indicate that an instruction, which might normally
refer to a 32-bit register, should instead refer to a 64-bit register. Also
some other things but we're mostly concerned with register sizes.

#### Opcode

Take a look at Section 2.1.2 (page 529) for a brief explanation of opcodes. THe
gist is that the opcode is the *meat* of the instruction. It's what makes a MOV
a MOV and not a HALT. The other fields all modify the meaning given by this
field.

#### ModR/M and SIB

Take a look at Section 2.1.3 (page 529) for a brief explanation of ModR/M and
SIB bytes. The gist is that they encode what register sources and destinations
to use.

#### Displacement and immediates

Take a look at Section 2.1.4 (page 529) for a brief explanation of displacement
and immediate bytes. The gist is that they encode literal numbers used in the
instructions that don't encode registers or anything.

If you're confused, that's okay. It should maybe get clearer once we get our
hands dirty. Reading all of this information in a vacuum is moderately useless
if it's your first time dealing with assembly like this, but I included this
first to help explain how to use the reference.

### Encoding, piece by piece

Got all that? Maybe? No? Yeah, me neither. But let's forge ahead anyway. Here's
the instruction we're going to encode: `REX.W + C7 /0 id`.

#### REX.W

First, let's figure out `REX.W`. According to Section 2.2.1, which explains REX
prefixes in some detail, there are a couple of different prefixes. There's a
helpful table (Table 2-4, page 535) documenting them. Here's a bit diagram with
the same information:

```
High           Low
[0100][W][R][X][B]
```

In English, and zero-indexed:

* Bits 7-4 are always `0b0100`.
* Bit 3 is the W prefix. If it's 1, it means the operands are 64 bits. If it's
  0, "operand size [is] determined by CS.D". Not sure what that means.
* Bits 2, 1, and 0 are other types of REX prefixes that we may not end up
  using, so I am omitting them here. Please read further in the manual if you
  are curious!

This MOV instruction calls for REX.W, which means this byte will look like
`0b01001000`, also known as our friend `0x48`. Mystery number one, solved!

#### C7

This is a hexadecimal literal `0xc7`. It is the *opcode*. There are a couple of
other entries with the opcode `C7`, modified by other bytes in the instruction
(ModR/M, SIB, REX, ...). Write it to the instruction stream. Mystery number
two, solved!

#### /0

There's a snippet in Section 2.1.5 that explains this notation:

> If the instruction does not require a second operand, then the Reg/Opcode
> field may be used as an opcode extension. This use is represented by the
> sixth row in the tables (labeled "/digit (Opcode)"). Note that values in row
> six are represented in decimal form.

This is a little confusing because this operation clearly *does* have a second
operand, denoted by the "MI" in the table, which shows Operand 1 being
`ModRM:r/m (w)` and Operand 2 being `imm8/16/32/64`. I think it's because it
doesn't have a second *register* operand that this space is free --- the
immediate is in a different place in the instruction.

In any case, this means that we have to make sure to put decimal `0` in the
`reg` part of the ModR/M byte.

#### id

*id* refers to an immediate *double word* (32 bits). It's called a *double*
word because, a word (*iw*) is 16 bits. In increasing order of size, we have:

* *ib*, byte (1 byte)
* *iw*, word (2 bytes)
* *id*, double word (4 bytes)
* *io*, quad word (8 bytes)

This means we have to write our 32-bit value out to the instruction stream.
These notations and encodings are explained further in Section 3.1.1.1 (page
596).

This is how you read the table! Slowly, piece by piece, and with a nice cup of
tea to help you in trying times. Now that we've read the table, let's go on and
write some code.

### Encoding, programatically

While writing code, you will often need to reference *two more tables* than the
ones we have looked at so far. These tables are Table 2-2 "32-Bit Addressing
Forms with the ModR/M Byte" (page 532) and Table 2-3 "32-Bit Addressing Forms
with the SIB Byte" (page 533). Although the tables describe 32-bit quantities,
with the REX prefix all the Es get replaced with Rs and all of a sudden they
can describe 64-bit quantities.

These tables are super helpful when figuring out how to put together ModR/M and
SIB bytes.

Let's start the encoding process by revisiting `Emit_mov_reg_imm32`/`REX.W + C7
/0 id`:

```c
void Emit_mov_reg_imm32(Buffer *buf, Register dst, int32_t src) {
  // ...
}
```

Given a register `dst` and an immediate 32-bit integer `src`, we're going to
encode this instruction. Let's do all the steps in order.

#### REX prefix

Since the instruction calls for REX.W, we can keep the first line the same as
before:

```c
void Emit_mov_reg_imm32(Buffer *buf, Register dst, int32_t src) {
  Buffer_write8(buf, kRexPrefix);
  // ...
}
```

Nice.

#### Opcode

This opcode is `0xc7`, so we'll write that directly:

```c
void Emit_mov_reg_imm32(Buffer *buf, Register dst, int32_t src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0xc7);
  // ...
}
```

Also the same as before. Nice.

#### ModR/M byte

ModR/M bytes are where the code gets a little different. We want an abstraction
to build them for us, instead of manually slinging integers like some kind of
animal.

To do that, we should know how they are put together. ModR/M bytes are
comprised of:

* *mod* (high 2 bits), which describes what big row to use in the ModR/M table
* *reg* (middle 3 bits), which either describes the second register operand
  *or* an opcode extension (like `/0` above)
* *rm* (low 3 bits), which describes the first operand

This means we can write a function `modrm` that puts these values together for
us:

```c
byte modrm(byte mod, byte rm, byte reg) {
  return ((mod & 0x3) << 6) | ((reg & 0x7) << 3) | (rm & 0x7);
}
```

The order of the parameters is a little different than the order of the bits. I
did this because it looks a little more natural when calling the function from
its callers. Maybe I'll change it later because it's too confusing.

For this instruction, we're going to:

* pass `0b11` (3) as *mod*, because we want to move directly into a 64-bit
  register, as opposed to `[reg]`, which means that we want to dereference the
  value in the pointer
* pass the destination register `dst` as *rm*, since it's the first operand
* pass `0b000` (0) as *reg*, since the `/0` above told us to

That ends up looking like this:

```c
void Emit_mov_reg_imm32(Buffer *buf, Register dst, int32_t src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0xc7);
  Buffer_write8(buf, modrm(/*direct*/ 3, dst, 0));
  // ...
}
```

I haven't put a datatype for *mod*s together because I don't know if I'd be
able to express it well. So for now I just added a comment.

#### Immediate value

Last, we have the immediate value. As I said above, all this entails is writing
out a 32-bit quantity as we have always done:

```c
void Emit_mov_reg_imm32(Buffer *buf, Register dst, int32_t src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0xc7);
  Buffer_write8(buf, modrm(/*direct*/ 3, dst, 0));
  Buffer_write32(buf, src);
}
```

And there you have it! It took us 2500 words to get us to these measly four
bytes. The real success is the friends we made along the way.

### Further instructions

"But Max," you say, "this produces literally the same output as before with all
cases! Why go to all this trouble? What gives?"

Well, dear reader, having a mod of 3 (direct) means that there is no
special-case escape hatch when `dst` is RSP. This is unlike the other mods,
where there's this `[--][--]` in the table where RSP should be. That funky
symbol indicates that there must be a Scale-Index-Base (SIB) byte following the
ModR/M byte.

This is where an instruction like `Emit_store_reg_indirect` (`mov [REG+disp],
src`) goes horribly awry with the homebrew encoding scheme I cooked up. When
the `dst` in that instruction is RSP, it's expected that the next byte is the
SIB. And when you output other data instead (say, an immediate 8-bit
displacement), you get really funky addressing modes. Like what the heck is
this?

```
mov qword [rsp + rax*2 - 8], rax
```

This is actual disassembled assembly that I got from running my binary code
through `rasm2`. Our compiler *definitely* does not emit anything that
complicated, which is how I found out things were wrong.

Okay, so it's wrong. We can't just blindly multiply and add things. So what do
we do?

#### The SIB byte

Take a look at Table 2-2 (page 532) again. See that trying to use RSP with any
sort of displacement requires the SIB.

Now take a look at Table 2-3 (page 533) again. We'll use this to put together
the SIB.

We know from Section 2.1.3 that the SIB, like the ModR/M, is comprised of three
fields:

* *scale* (high 2 bits), specifies the scale factor
* *index* (middle 3 bits), specifies the register number of the index register
* *base* (low 3 bits), specifies the register number of the base register

Intel's language is not so clear and is kind of circular. Let's take a look at
sample instruction to clear things up:

````
mov [base + index*scale + disp], src
```

Note that while *index* and *base* refer to registers, *scale* refers to one
of 1, 2, 4, or 8, and *disp* is some immediate value.

This is a compact way of specifying a memory offset. It's convenient for
reading from and writing to arrays and structs. It's also going to be necessary
for us if we want to write to and read from random offsets from the stack
pointer, RSP.

So let's try and encode that `Emit_store_reg_indirect`.

#### Encoding the indirect mov

Let's start by going back to the table enumerating all the kinds of MOV
instructions (page 1209). The specific opcode we're looking for is `REX.W + 89
/r`, or `MOV r/m64, r64`.

We already know what REX.W means:

```c
void Emit_store_reg_indirect(Buffer *buf, Indirect dst, Register src) {
  Buffer_write8(buf, kRexPrefix);
  // ...
}
```

And next up is the literal `0x89`, so we can write that straight out:

```c
void Emit_store_reg_indirect(Buffer *buf, Indirect dst, Register src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0x89);
  // ...
}
```

So far, so good. Looking familiar. Now that we have both the instruction prefix
and the opcode, it's time to write the ModR/M byte. Our ModR/M will contain
the following information:

* *mod* of 1, since we want an 8-bit displacement
* *reg* of whatever register the second operand is, since we have two register
  operands (the opcode field says `/r`)
* *rm* of whatever register the first operand is

Alright, let's put that together with our handy-dandy ModR/M function.

```c
void Emit_store_reg_indirect(Buffer *buf, Indirect dst, Register src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0x89);
  // Wrong!
  Buffer_write8(buf, modrm(/*disp8*/ 1, dst.reg, src));
  // ...
}
```

But no, this is wrong. As it turns out, you still have do this special thing
when `dst.reg` is RSP, as I keep mentioning. In that case, `rm` must be the
special *none* value (as specified by the table). Then you also have to write a
SIB byte.

```c
void Emit_store_reg_indirect(Buffer *buf, Indirect dst, Register src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0x89);
  if (dst.reg == kRsp) {
    Buffer_write8(buf, modrm(/*disp8*/ 1, kIndexNone, src));
    // ...
  } else {
    Buffer_write8(buf, modrm(/*disp8*/ 1, dst.reg, src));
  }
  // ...
}
```

Let's go ahead and write that SIB byte. I made a `sib` helper function like
`modrm`, with two small differences: the parameters are in order of high to
low, and the parameters have their own special types instead of just being
`byte`s.

```c
typedef enum {
  Scale1 = 0,
  Scale2,
  Scale4,
  Scale8,
} Scale;

typedef enum {
  kIndexRax = 0,
  kIndexRcx,
  kIndexRdx,
  kIndexRbx,
  kIndexNone,
  kIndexRbp,
  kIndexRsi,
  kIndexRdi
} Index;

byte sib(Register base, Index index, Scale scale) {
  return ((scale & 0x3) << 6) | ((index & 0x7) << 3) | (base & 0x7);
}
```

I made all these datatypes to help readability, but you don't have to use them
if you don't want to. The `Index` one is the only one that has a small gotcha:
where `kIndexRsp` should be is `kIndexNone` because you can't use RSP as an
index register.

Let's use this function to write a SIB byte in `Emit_store_reg_indirect`:

```c
void Emit_store_reg_indirect(Buffer *buf, Indirect dst, Register src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0x89);
  if (dst.reg == kRsp) {
    Buffer_write8(buf, modrm(/*disp8*/ 1, kIndexNone, src));
    Buffer_write8(buf, sib(kRsp, kIndexNone, Scale1));
  } else {
    Buffer_write8(buf, modrm(/*disp8*/ 1, dst.reg, src));
  }
  // ...
}
```

This is a very verbose way of saying `[rsp+DISP]`, but it'll do. All that's
left now is to encode that displacement. To do that, we'll just write it out:

```c
void Emit_store_reg_indirect(Buffer *buf, Indirect dst, Register src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0x89);
  if (dst.reg == kRsp) {
    Buffer_write8(buf, modrm(/*disp8*/ 1, kIndexNone, src));
    Buffer_write8(buf, sib(kRsp, kIndexNone, Scale1));
  } else {
    Buffer_write8(buf, modrm(/*disp8*/ 1, dst.reg, src));
  }
  Buffer_write8(buf, disp8(indirect.disp));
}
```

Very nice. Now it's your turn to go forth and convert the rest of the assembly
functions in your compiler! I found it very helpful to extract the
`modrm`/`sib`/`disp8` calls into a helper function, because they're mostly the
same and very repetitive.

### What did we learn?

This was a very long post. The longest post in the whole series so far, even.
We should probably have some concrete takeaways.

If you read this post through, you should have gleaned some facts and lessons
about:

* Intel x86-64 instruction encoding terminology and details, and
* how to read dense tables in the Intel Developers Manual
* maybe some third thing, too, I dunno --- this post was kind of a lot

Hopefully you enjoyed it. I'm going to go try and get a good night's sleep.
Until next time, when we'll implement procedure calls!


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

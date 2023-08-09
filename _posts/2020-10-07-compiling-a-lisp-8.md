---
title: "Compiling a Lisp: If"
layout: post
date: 2020-10-07 11:00:00 PT
description: Compiling Lisp if-expressions to x86-64
og_image: /assets/img/compiling-a-lisp-og-image.png
series: compiling-a-lisp
---

<span data-nosnippet>
*[first](/blog/compiling-a-lisp-0/)* -- *[previous](/blog/compiling-a-lisp-7/)*
</span>

Welcome back to the "Compiling a Lisp" series. Last time we added support for
`let` expressions. This time we're going to compile `if` expressions.

Compiling `if` expressions will allow us to write code that performs decision
making. For example, we can write code that does something based on the result
of some imaginary function `coin-flip`:

```common-lisp
(if (= (coin-flip) heads)
    123
    456)
```

If the call to `coin-flip` returns `heads`, then this whole if-expression will
evaluate to `123`. Otherwise, it will evaluate to `456`. To determine if an
expression is truthy, we'll check if it is not equal to `#f`.

Note that the iftrue and iffalse expressions (*consequent* and *alternate*,
respectively) are only evaluated if their branch is reached.

### Implementation strategy

People normally compile `if` expressions by taking the following structure in
Lisp:

```common-lisp
(if condition
    consequent
    alternate)
```

and rewriting it to the following pseudo-assembly (where `...compile(X)` is
replaced with compiled code from the expressions):

```
  ...compile(condition)
  compare result, #f
  jump-if-equal alternate
  ...compile(consequent)
  jump end
alternate:
  ...compile(alternate)
end:
```

This will evaluate the `condition` expression. If it's falsey, jump to the code
for the `alternate` expression. Otherwise, continue on to the code for the
`consequent` expression. So that the program does not also execute the code for
the `alternate`, jump over it.

This transformation requires a couple of new pieces of infrastructure.

### Implementation infrastructure

First, we'll need two types of jump instructions! We have a conditional jump
(`jcc` in x86-64) and an unconditional jump (`jmp` in x86-64). These are
relatively straightforward to emit.

Somewhat more complicated are the *targets* of those jump instructions. We'll
need to supply each of the instructions with some sort of destination code
address.

When emitting text assembly, this is not so hard: make up names for your labels
(as with `alternate` and `end` above), and keep the names consistent between
the jump instruction and the jump target. Sure, you need to generate *unique*
labels, but the assembler will at least do address resolution for you. This
address resolution transparently handles backward jumps (where the label is
already known) and forward jumps (where the label comes after the jump
instruction).

Since we're not emitting text assembly, we'll need to calculate both forward
and backward jump offsets by hand. This ends up not being so bad in practice
once we come up with an ergonomic way to do it. Let's take a look at some
production-grade assemblers for inspiration.

### How Big Kid compilers do this

I read some source code for assemblers like the [Dart
assembler][dart-assembler]. Dart is a language runtime developed by Google and
part of their infrastructure includes a Just-In-Time compiler, sort of like
what we're making here. Part of their assembler is some slick C++-y RAII
infrastucture for emitting code and doing cleanup. Their implementation of
compiling `if` expressions might look something like:

[dart-assembler]: https://github.com/dart-lang/sdk/blob/2707880f1b486f2a2f87d7e2de7baea12c6ca362/runtime/vm/compiler/assembler/assembler_base.h

```cpp
// Made-up APIs to make the Dart code look like our code
int Compile_if(Buffer *buf, ASTNode *cond, ASTNode *consequent,
               ASTNode *alternate) {
   Label alternate;
   Label end;
   compile(buf, cond);
   buf->cmp(kRax, Object::false());
   buf->jcc(kEqual, &alternate);
   compile(buf, consequent);
   buf->jmp(&end);
   buf->bind(&alternate);
   compile(buf, alternate);
   buf->bind(&end);
}
```

Their `Label` objects store information about where in the emitted machine code
they are bound with `bind`. If they are bound before they are used by `jcc` or
`jmp` or something, then the emitter will just emit the destination address. If
they are *not* yet bound, however, then the `Label` will keep track of where it
has to go back and patch the machine code once the label is bound to a
location.

When the labels are destructed --- meaning they can no longer be referenced by
C++ code --- their destructors have code to go back and patch all the
instructions that referenced the label before it was bound.

While x86-64 has multiple jump widths available (for speed, I guess), it is a
little tricky to use them for forward jumps. Because we don't know in advance
how long the intermediate code will be, we'll just stick to generating 32-bit
relative jumps *always*.

<a name="assembler-libraries"></a>

Virtual Machines like ART, [OpenJDK Hotspot][openjdk-label],
[SpiderMonkey][spidermonkey-label], V8, [HHVM][hhvm-label], and
[Strongtalk][strongtalk-label] also use this approach. So do the VM-agnostic
[AsmJit][asmjit-label] and [GNU lightning][lightning-docs] assemblers. If I
didn't link an implementation, it's either because I found the it too
complicated to reproduce or couldn't quickly track it down. Or maybe I don't
know about it!

[openjdk-label]: https://github.com/openjdk/jdk/blob/3e0dc6888329b3b4ba9cc357a01a775044f1cb3c/src/hotspot/share/asm/assembler.hpp
[spidermonkey-label]: https://github.com/servo/mozjs/blob/538693677007cc22f90e299104e885198e3ee748/mozjs/js/src/jit/Label.h
[strongtalk-label]: https://github.com/talksmall/Strongtalk/blob/39b336f8399230502535e7ac12c9c1814552e6da/vm/asm/assembler.hpp
[hhvm-label]: https://github.com/facebook/hhvm/blob/e372db1ba0dcdd587c5d54d859f799b0bee265bb/hphp/vixl/a64/assembler-a64.h
[asmjit-label]: https://github.com/asmjit/asmjit/blob/cd44f41d9b34ed4bd5f2ea2da5ed460d52bd1134/src/asmjit/x86/x86compiler.h
[lightning-docs]: http://www.gnu.org/software/lightning/manual/lightning.html

Basically what I am trying to tell you is that this bind-and-backpatch approach
is tried and tested and that we're going to implement it in C. I hope you
enjoyed the whirlwind tour of assemblers in various other runtimes along the
way.

### Compiling `if`-expressions, finally

Alright, so we finally get the big idea about how to do this transformation.
Let's put it into practice.

First, as with `let`, we're going to need to handle the `if` case in
`Compile_call`.

```c
int Compile_call(Buffer *buf, ASTNode *callable, ASTNode *args,
                 word stack_index, Env *varenv) {
  if (AST_is_symbol(callable)) {
    // ...
    if (AST_symbol_matches(callable, "if")) {
      return Compile_if(buf, /*condition=*/operand1(args),
                        /*consequent=*/operand2(args),
                        /*alternate=*/operand3(args), stack_index, varenv);
    }
  }
  // ...
}
```

As usual, we'll pull apart the expression so `Compile_if` has less work to do.
Since we now have more than two operands (!), I've added `operand3`. It works
just like you would think it does.

For `Compile_if`, we're going to largely replicate the pseudocode C++ from
above. I think you'll find that if you squint it looks similar enough.

```c
int Compile_if(Buffer *buf, ASTNode *cond, ASTNode *consequent,
               ASTNode *alternate, word stack_index, Env *varenv) {
  _(Compile_expr(buf, cond, stack_index, varenv));
  Emit_cmp_reg_imm32(buf, kRax, Object_false());
  word alternate_pos = Emit_jcc(buf, kEqual, kLabelPlaceholder); // je alternate
  _(Compile_expr(buf, consequent, stack_index, varenv));
  word end_pos = Emit_jmp(buf, kLabelPlaceholder); // jmp end
  Emit_backpatch_imm32(buf, alternate_pos);        // alternate:
  _(Compile_expr(buf, alternate, stack_index, varenv));
  Emit_backpatch_imm32(buf, end_pos); // end:
  return 0;
}
```

Instead of having a `Label` struct, though, I opted to just have a function to
backpatch forward jumps explicitly. If you prefer to port `Label` to C, be my
guest. I found it very finicky[^1].

Also, instead of `bind`, I opted for a more explicit `backpatch`. This makes it
clearer what is happening, I think.

This explicit backpatch approach requires manually tracking the offsets (like
`alternate_pos` and `end_pos`) inside the jump instructions. We'll need those
offsets to backpatch them later. This means functions like `Emit_jcc` and
`Emit_jmp` should return the offsets inside `buf` where they write placeholder
offsets.

Let's take a look inside these helper functions' internals.

### `jcc` and `jmp` implementations

The implementations for `jcc` and `jmp` are pretty similar, so I will only
reproduce `jcc` here.

```c
word Emit_jcc(Buffer *buf, Condition cond, int32_t offset) {
  Buffer_write8(buf, 0x0f);
  Buffer_write8(buf, 0x80 + cond);
  word pos = Buffer_len(buf);
  Buffer_write32(buf, disp32(offset));
  return pos;
}
```

This function is like many other `Emit` functions except for its return value.
It returns the start location of the 32-bit offset for use in patching forward
jumps. In the case of backward jumps, we can ignore this, since there's no need
to patch it after-the-fact.

### Backpatching implementation

Here is the implementation of `Emit_backpatch_imm32`. I'll walk through it and
explain.

```c
void Emit_backpatch_imm32(Buffer *buf, int32_t target_pos) {
  word current_pos = Buffer_len(buf);
  word relative_pos = current_pos - target_pos - sizeof(int32_t);
  Buffer_at_put32(buf, target_pos, disp32(relative_pos));
}
```

The input `target_pos` is the location inside the `jmp` (or similar)
instruction that needs to be patched. Since we need to patch it with a
*relative* offset, we compute the distance between the current position and the
target position. We also need to subtract 4 bytes (`sizeof(int32_t)`) because
the jump offset is relative to the *end* of the `jmp` instruction (the
beginning of the next instruction).

Then, we write that value in. `Buffer_at_put32` and `disp32` are similar to
their 8-bit equivalents.

Congratulations! You have implemented `if`.

### A fun diagram

Radare2 has a tool called [Cutter](https://cutter.re/) for reverse engineering
and binary analysis. I decided to use it on the compiled output of a function
containing an `if` expression. It produced this pretty graph!

<figure style="display: block; margin: 0 auto; max-width: 600px;">
  <img style="max-width: 600px;" src="/assets/img/lisp/callgraph.svg" />
  <figcaption>Fig. 1 - Call graph as produced by Cutter.</figcaption>
</figure>

It's prettier in the tool, trust me.

### Tests

I added two trivial tests for the condition being true and the condition being
false. I also added a nested if case as a smoke test but I did not foresee that
being troublesome with our handy recursive approach.

```c
TEST compile_if_with_true_cond(Buffer *buf) {
  ASTNode *node = Reader_read("(if #t 1 2)");
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  byte expected[] = {
      // mov rax, 0x9f
      0x48, 0xc7, 0xc0, 0x9f, 0x00, 0x00, 0x00,
      // cmp rax, 0x1f
      0x48, 0x3d, 0x1f, 0x00, 0x00, 0x00,
      // je alternate
      0x0f, 0x84, 0x0c, 0x00, 0x00, 0x00,
      // mov rax, compile(1)
      0x48, 0xc7, 0xc0, 0x04, 0x00, 0x00, 0x00,
      // jmp end
      0xe9, 0x07, 0x00, 0x00, 0x00,
      // alternate:
      // mov rax, compile(2)
      0x48, 0xc7, 0xc0, 0x08, 0x00, 0x00, 0x00
      // end:
  };
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ_FMT(Object_encode_integer(1), result, "0x%lx");
  AST_heap_free(node);
  PASS();
}

TEST compile_if_with_false_cond(Buffer *buf) {
  ASTNode *node = Reader_read("(if #f 1 2)");
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  byte expected[] = {
      // mov rax, 0x1f
      0x48, 0xc7, 0xc0, 0x1f, 0x00, 0x00, 0x00,
      // cmp rax, 0x1f
      0x48, 0x3d, 0x1f, 0x00, 0x00, 0x00,
      // je alternate
      0x0f, 0x84, 0x0c, 0x00, 0x00, 0x00,
      // mov rax, compile(1)
      0x48, 0xc7, 0xc0, 0x04, 0x00, 0x00, 0x00,
      // jmp end
      0xe9, 0x07, 0x00, 0x00, 0x00,
      // alternate:
      // mov rax, compile(2)
      0x48, 0xc7, 0xc0, 0x08, 0x00, 0x00, 0x00
      // end:
  };
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ_FMT(Object_encode_integer(2), result, "0x%lx");
  AST_heap_free(node);
  PASS();
}
```

I made sure to test the generated code because we added some new instructions
and also because I had trouble getting the offset computations perfectly right
initially.

Anyway, that's all for today. This post was made possible by contributions[^2]
to my blog from Viewers Like You. Thank you.

Next time on PBS, [heap allocation](/blog/compiling-a-lisp-9/).

{% include compiling_a_lisp_toc.md %}

[^1]: Maybe it would be less finicky with `__attribute__((cleanup))`, but that
      is non-standard. This [StackOverflow][cleanup] question and associated
      answers have some good information.

      [cleanup]: https://stackoverflow.com/questions/34574933/a-good-and-idiomatic-way-to-use-gcc-and-clang-attribute-cleanup-and-point

[^2]: By "contributions" I mean thoughtful comments, questions, and
      appreciation. Feel free to chime in on Twitter, HN, Reddit, lobste.rs,
      the [mailing list](https://lists.sr.ht/~max/compiling-lisp)...

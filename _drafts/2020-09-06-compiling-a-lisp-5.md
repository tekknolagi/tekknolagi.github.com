---
title: "Compiling a Lisp: Primitive binary functions"
layout: post
date: 2020-09-06 9:00:00 PDT
---

*[first]({% link _posts/2020-08-29-compiling-a-lisp-0.md %})* -- *[previous]({% link _posts/2020-09-05-compiling-a-lisp-4.md %})*

Welcome back to the "Compiling a Lisp" series. Last time, we added some
primitive unary instructions like `add1` and `integer->char`. This time, we're
going to add some primitive *binary* functions like `+` and `>`. After this
post, we'll be able to write programs like:

```common-lisp
(< (+ 1 2) (- 4 3))
```

Now that we're building primitive functions that can take two arguments, you
might notice a problem: our strategy of just storing the result in `rax` wont't
work. If we were to na&iuml;vely write something like the following to
implement `+`, then `rax` would get overwritten:

```c
int Compile_call(Buffer *buf, ASTNode *callable, ASTNode *args,
                 word stack_index) {
  if (AST_is_symbol(callable)) {
    // ...
    if (AST_symbol_matches(callable, "+")) {
      _(Compile_expr(buf, operand2(args)));
      _(Compile_expr(buf, operand1(args)));
      // Oops, we just overwrote rax ^
      Emit_add_something(buf, /*dst=*/kRax));
      return 0;
    }
    // ...
  }
  // ...
}
```

We could try and work around this by adding some kind of register allocation
algorithm. Or, simpler, we could decide to allocate all intermediate values on
the stack and move on with our lives. I prefer the latter.

### Register allocation

Let's take a look at the stack at the moment we enter the compiled program:

```
+------------------+ High addresses
|  main            |
+------------------+ |
|  ~ some data ~   | |
|  ~ some data ~   | |
+------------------+ |
|  compile_test    | |
+------------------+ |
|  ~ some data ~   | |
|  ~ some data ~   | v
+------------------+
|  Testing_exe...  | rsp
+------------------+
|                  | Low addresses
```

Where `rsp` denotes the 64 bit register for the Stack Pointer.

Refresher: the call stack grows *down*. Why? Check out this [StackOverflow
answer](https://stackoverflow.com/a/54391533/569183) that quotes an architect
on the Intel 4004 and 8080 architectures.

We have `rsp` pointing at a return address inside the function
`Testing_execute_expr`, since that's what called our Lisp entrypoint. We have
some data above `rsp` that we're not allowed to poke at, and we have this empty
space below `rsp` that is in our current *stack frame*. I say "empty" because
we haven't yet stored anything there, but because it's necessarily zero-ed out.
I don't think there are any guarantees about the values in this stack frame.

We can use our stack frame to write and read values for our current Lisp
program. With every recursive subexpression, we can allocate a little more
stack space to keep track of the values. When I say "allocate", I mean
"subtract from the stack pointer", because the stack is already a contiguous
space in memory allocated for us. For example, here is how we can write to the
stack:

```
mov [rsp-8], 0x4
```

This puts the integer `4` at displacement `-8` from `rsp`. On the stack diagram
above, it would be at the slot labeled "Low addresses". It's also possible
to read with a positive or zero displacement, but those point to previous stack
frames and the return address, respectively. So let's avoid manipulating those.

> Note that I used a multiple of 8. Not every store has to be a to an address
> that is a multiple of 8, but it is natural and I think also *faster* to store
> 8-byte-sized things at aligned addresses.

Let's walk through a real example to get more hands-on experience with this
stack storage idea. We'll use the program `(+ 1 2)`. The compiled version of
that program should:

* Move `encode(2)` to `rax`
* Move `rax` into `[rsp-8]`
* Move `encode(1)` to `rax`
* Add `[rsp-8]` to `rax`

So after compiling that, the stack will look like this:

```
+------------------+ High addresses
|  Testing_exe...  | RSP
+------------------+
|  0x8             | RSP-8 (result of compile(2))
|                  | Low addresses
```

And the result will be in `rax`.

This is all well and good, but at some point we'll need our compiled programs
to emit the `push` instruction or make function calls of their own. Both of
these modify the stack pointer. `push` writes to the stack and decrements
`rsp`. `call` is roughly equivalent to `push` followed by `jmp`.

For that reason, x86-64 comes with another register called `rbp` and it's
designed to hold the Base Pointer. While the stack pointer is supposed to track
the "top" (low address) of the stack, the base pointer is meant to keep a
pointer around to the "bottom" (high address) of our current stack frame.

This is why in a lot of compiled code you see the following instructions
repeated[^1]:

```nasm
myfunction:
push rbp
mov rbp, rsp
sub rsp, N  ; optional; allocate stack space for locals
; ... function body ...
mov rsp, rbp  ; required if you subtracted from rsp above
pop rbp
ret
```

The first three instructions, called the *prologue*, save `rbp` to the stack,
and then set `rbp` to the current stack pointer. Then `rsp` is freely
changeable and it's still possible to maintain references to variables on the
stack without adjusting them.

The last three instructions, called the *epilogue*, fetch the old `rbp` that we
saved to the stack, writes it back into `rbp`, then exit the call.

To confirm this for yourself, check out this [sample compiled C
code](https://godbolt.org/z/qPM8Mh). Look at the disassembly following the
label `square`. Prologue, code, epilogue.

### Stack allocation infrastructure

Until now, we haven't needed to keep track of much as we recursively traverse
expression trees. Now, in order to keep track of how much space on the stack
any given compiled code will need, we have to add more state to our compiler.
We'll call this state the `stack_index` --- Ghuloum calls it `si` --- and we'll
pass it around as a parameter. Whatever it's called, it points to the first
writable (unused) index in the stack at any given point.

In compiled function, the first writable index is `-kWordSize` (`-8`), since
the base pointers is at `0`.

```c
int Compile_function(Buffer *buf, ASTNode *node) {
  Buffer_write_arr(buf, kFunctionPrologue, sizeof kFunctionPrologue);
  _(Compile_expr(buf, node, -kWordSize));
  Buffer_write_arr(buf, kFunctionEpilogue, sizeof kFunctionEpilogue);
  return 0;
}
```

I've also gone ahead and added the prologue and epilogue.

For `Compile_expr`, we just pass this new stack index through.

```c
int Compile_expr(Buffer *buf, ASTNode *node, word stack_index) {
  // ...
  if (AST_is_pair(node)) {
    return Compile_call(buf, AST_pair_car(node), AST_pair_cdr(node),
                        stack_index);
  }
  // ...
}
```

And for `Compile_call`, we actually get to use it. Let's look back at our stack
storage strategy for compiling `(+ 1 2)` (now replacing `rsp` with `rbp`):

* Move `encode(2)` to `rax`
* Move `rax` into `[rbp-8]`
* Move `encode(1)` to `rax`
* Add `[rbp-8]` to `rax`

For binary functions, this can be generalized to:

* Compile `arg2` (stored in `rax`)
* Move `rax` to `stack_index`
* Compile `arg1` (stored in `rax`)
* Do something with the results (in `[rbp-stack_index]` and `rax`)

The key is this: for the first recursive call to `Compile_expr`, the compiler
is allowed to emit code that can use the current `stack_index` and anything
below that on the stack. For the *second* recursive call to `Compile_expr`, the
compiler has to bump `stack_index`, since we've stored the result of the first
compiled call at `stack_index`.

Take a look:

```c
int Compile_call(Buffer *buf, ASTNode *callable, ASTNode *args,
                 word stack_index) {
  if (AST_is_symbol(callable)) {
    // ...
    if (AST_symbol_matches(callable, "+")) {
      _(Compile_expr(buf, operand2(args), stack_index));
      Emit_store_reg_indirect(buf, /*dst=*/Ind(kRbp, stack_index),
                              /*src=*/kRax);
      _(Compile_expr(buf, operand1(args), stack_index - kWordSize));
      Emit_add_reg_indirect(buf, /*dst=*/kRax, /*src=*/Ind(kRbp, stack_index));
      return 0;
    }
    // ...
  }
  // ...
}
```

In this snippet, `Ind` stands for "indirect". This is a function that makes a
struct --- it's an easy and readable way to represent (register, displacement)
pairs for use in reading from and writing to memory. We'll cover this more
detail in the instruction encoding.

To prove to myself that this works, I put some print statements in to write out
what expression was getting compiled and where it was getting stored. I tried
then compiling `(+ (+ 1 2) (+ 3 4))`. The output looks like this:

```
stored right 4 in [rbp-8]
stored left 3 in rax
stored add result in rax
stored right (+ 3 4) in [rbp-8]
stored right 2 in [rbp-16]
stored left 1 in rax
stored add result in rax
stored left (+ 1 2) in rax
stored add result in rax
```

Which is a little hard to read since it's a post-order traversal, but if you
read the expression backwards it kind of makes sense when you squint. I tried
making a diagram out of this, believe me, but I couldn't figure out something
readable. Whatever. We'll have tests later for this reason.

### Other binary functions

Subtraction, multiplication, and division are much the same as addition. We're
also going to completely ignore overflow, underflow, etc.

Equality is different in that it does some comparisons after the fact (see
[Primitive unary functions]({% link _posts/2020-09-05-compiling-a-lisp-4.md
%})). To check if two values are equal, we compare their pointers:

```c
    if (AST_symbol_matches(callable, "=")) {
      _(Compile_expr(buf, operand2(args), stack_index));
      Emit_store_reg_indirect(buf, /*dst=*/Ind(kRbp, stack_index),
                              /*src=*/kRax);
      _(Compile_expr(buf, operand1(args), stack_index - kWordSize));
      Emit_cmp_reg_indirect(buf, kRax, Ind(kRbp, stack_index));
      Emit_mov_reg_imm32(buf, kRax, 0);
      Emit_setcc_imm8(buf, kEqual, kAl);
      Emit_shl_reg_imm8(buf, kRax, kBoolShift);
      Emit_or_reg_imm8(buf, kRax, kBoolTag);
      return 0;
    }
```

It uses a new comparison opcode that compares a register with some memory. This
is why we can't use the `Compile_compare_imm32` helper function.

The less-than operator (`<`) is very similar to equality, but instead we use
`setcc` with the `kLess` flag instead of the `kEqual` flag.

### New opcodes

We used some new opcodes today, so let's take a look at the implementations.
First, here is the indirection implementation I mentioned earlier:

```c
typedef struct Indirect {
  Register reg;
  int8_t disp;
} Indirect;

Indirect Ind(Register reg, int8_t disp) {
  return (Indirect){.reg = reg, .disp = disp};
}
```

I would have used the same name in the struct and the constructor but
unfortunately that's not allowed.

Here's an implementation of an opcode that uses this `Indirect` type. This
emits code for `mov [dst+disp], src`.

```c
uint8_t disp8(int8_t disp) { return disp >= 0 ? disp : 0x100 + disp; }

void Emit_store_reg_indirect(Buffer *buf, Indirect dst, Register src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0x89);
  Buffer_write8(buf, 0x40 + src * 8 + dst.reg);
  Buffer_write8(buf, disp8(dst.disp));
}
```

The `disp8` function is a helper that encodes negative numbers.

The opcodes for `add`, `sub`, and `cmp` are similar enough to this one, except
`src` and `dst` are swapped. `mul` is a little funky because it doesn't take
two parameters. It assumes that one of the operands is always in `rax`.

### Testing

As usual, we'll close with some snippets of tests.

Here's a test for `+`. I'm trying to see if inlining the text assembly with the
hex makes it more readable. Thanks [Kartik](http://akkartik.name/) for the
suggestion.

```c
TEST compile_binary_plus(Buffer *buf) {
  ASTNode *node = new_binary_call("+", AST_new_integer(5), AST_new_integer(8));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  byte expected[] = {
      // 0:  48 c7 c0 20 00 00 00    mov    rax,0x20
      0x48, 0xc7, 0xc0, 0x20, 0x00, 0x00, 0x00,
      // 7:  48 89 45 f8             mov    QWORD PTR [rbp-0x8],rax
      0x48, 0x89, 0x45, 0xf8,
      // b:  48 c7 c0 14 00 00 00    mov    rax,0x14
      0x48, 0xc7, 0xc0, 0x14, 0x00, 0x00, 0x00,
      // 12: 48 03 45 f8             add    rax,QWORD PTR [rbp-0x8]
      0x48, 0x03, 0x45, 0xf8};
  EXPECT_FUNCTION_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_integer(13));
  AST_heap_free(node);
  PASS();
}
```

Here's a test for `<`.

```c
TEST compile_binary_lt_with_left_greater_than_right_returns_false(Buffer *buf)
{
  ASTNode *node = new_binary_call("<", AST_new_integer(6), AST_new_integer(5));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ_FMT(Object_false(), result, "0x%lx");
  AST_heap_free(node);
  PASS();
}
```

This has been a more complicated post than the previous ones, I think. The
stack allocation may not make sense immediately. It might take some time to
sink in. Try writing some of the code yourself and see if that helps.

Next time we'll add the ability to bind variables using `let`. See you then!

<br />
<hr style="width: 100px;" />
<!-- Footnotes -->

[^1]: You may also see an `enter` instruction paired with a `leave`
      instruction. These are equivalent. Read more
      [here](https://en.wikipedia.org/wiki/Function_prologue).

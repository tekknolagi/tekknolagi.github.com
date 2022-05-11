---
title: "Compiling a Lisp: Heap allocation"
layout: post
date: 2020-10-11 00:00:00 PT
description: Compiling Lisp cons, car, and cdr to x86-64
og_image: /assets/img/compiling-a-lisp-og-image.png
series: compiling-a-lisp
---

<span data-nosnippet>
*[first](/blog/compiling-a-lisp-0/)* -- *[previous](/blog/compiling-a-lisp-8/)*
</span>

Welcome back to the "Compiling a Lisp" series. Last time we added support for
`if` expressions. This time we're going add support for basic heap allocation.

Heap allocation comes in a couple of forms, but the one we care about right now
is the `cons` primitive. Much like `AST_new_pair` in the compiler, `cons`
should:

* allocate some space on the heap,
* set the `car` and `cdr`, and
* tag the pointer appropriately.

Once we have that pair, we'll want to poke at its data. This means we should
probably also implement `car` and `cdr` primitive functions today.

### What a pair looks like in memory

In order to generate code for packing and pulling apart pairs, we should
probably know how they are laid out in memory.

Pairs contain two elements, side by side --- kind of like a two-element array.
The first element (`pair[0]`) is the `car` and the second one (`pair[1]`) is
the `cdr`.

```
   +-----+-----+
...| car | cdr |...
   +-----+-----+
   ^
   pointer
```

The untagged pointer points to the address of the first element, and the tagged
pointer has some extra information (`kPairTag` == `1`) that we need to get rid
of to inspect the elements. If we don't, we'll try and read from one byte after
the pointer, somewhere in the middle of the `car`. This will give us bad data.

To make things more concrete, imagine our pair is allocated at `0x10000`. Our
`car` lives at `*(0x10000)` (using C notation) and our `cdr` lives at
`*(0x10000 + kWordSize)`. The tagged pointer in this case would be `0x10001`
and `kWordSize` is 8.

### Allocating some memory

We *could* make a call to `malloc` whenever we need a new object. This has a
couple of downfalls, notably that `malloc` does a lot of internal bookkeeping
that we really don't need, and that there's no good way to keep track of what
memory we have allocated and needs garbage collecting (which we'll handle
later). It also has the unfortunate property of requiring C functional call
infrastructure, which we don't have yet.

What we're going to do instead is allocate a big slab of memory at the
beginning of our process. That will be our heap. Then, to keep track of what
memory we have used so far, we're going to bump the pointer every time we
allocate. So here's what the heap looks like before we allocate a pair:

```
+-----+-----+-----+-----+-----+-----+-----+-----+
|     |     |     |     |     |     |     |     |...
+-----+-----+-----+-----+-----+-----+-----+-----+
^
heap
```

The empty cells aren't necessarily empty, but they are unused and they are
garbage data.

Here is what it looks like after we allocate a pair:

```
+-----+-----+-----+-----+-----+-----+-----+-----+
| car | cdr |     |     |     |     |     |     |...
+-----+-----+-----+-----+-----+-----+-----+-----+
^           ^
pair        heap
```

Notice how the `heap` pointer has been moved over 2 words, and the `pair`
pointer is the returned cons cell. Although we'll tag the `pair` pointer, I am
pointing it at the beginning of the `car` for clarity in the diagram.

In order to get this big slab of memory in the first place, we'll have the
outside C code (right now, that's our test handler) call `malloc`.

You're probably wondering what we're going to do when we run out of memory. At
some point in this series we'll have a garbage collector that can reclaim some
space for us. Right now, though, we're just going to do ... nothing. That's
right, we won't even raise some kind of "out of memory" error. Remember, we
don't yet have error reporting facilities! Instead, we'll use tools like
Valgrind and AddressSanitizer to make sure we're not overrunning our allocated
buffer.

### Implementation strategy

In order to make allocation from that big buffer fast and easy, we're going to
keep the heap pointer in a register. Our compiler emits instructions that use
`rbp`, `rsp`, and `rax`, so we'll have to pick another one. Ghuloum uses `rsi`,
so we'll use that as well.

In order to get the heap pointer in `rsi` in the first place, we'll have to
capture it from the outside C code. To do this, we'll add a parameter to our
*entrypoint* by modifying the function prologue.

Remember `JitFunction`? This is what the C code uses to understand how to call
our `mmap`-ed function. We're going to need to modify this first.

```c
// Before:
typedef uword (*JitFunction)();

// After:
typedef uword (*JitFunction)(uword *heap);
```

That's going to need to take a new parameter now --- a pointer to the heap.
This means that our `kFunctionPrologue` will need to expect that in the first
parameter register in the calling convention, and store it somewhere safe. This
register is `rdi`, so we can emit a `mov rsi, rdi` to store our heap pointer
away.

Now, for the lifetime of the Lisp entrypoint, we can refer to the heap by the
name `rsi` and modify it accordingly. We'll keep an internal convention that
`rsi` *always* points to the next available chunk of memory.

Want to allocate memory? Copy the current heap pointer into `rax` and update
the heap pointer with `add rsi, AllocationSize`. We'll need to add a new
instruction for moving data between registers. Honestly, I am kind of surprised
we haven't needed that yet.

Want to store your `car` and `cdr` in your new pair? Write to offset `0` and
`kWordSize` of `rax`, respectively. We'll reuse our indirect store instruction.

Want to tag your pointer? `add rax, Tag` or `or rax, Tag`. These two
instructions are equivalent because all the three taggable bits in a heap
object will be zero.

> This word-alignment is easy to maintain now because all pairs will be size
> 16, which is a multiple of 8. Later on, when we add symbols and strings and
> other data types that have non-object data in them, we'll have to insert
> padding between allocations to keep the alignment invariant.

Once we have pairs allocated, it's kind of useless unless we can also poke at
their elements.

To implement `car`, we'll remove the tag from the pointer and read from the
memory pointed to by the register: `mov rax, [Ptr+Car-Tag]`. You can also do
this with a `sub rax, Tag` and then a `mov`.

Implementing `cdr` is very similar, except we'll be doing `mov rax,
[Ptr+Cdr-Tag]`.

### Brass tacks

Now that we've gotten our minds around the abstract solution to the problem, we
should write some code.

First off, here is the addition to the prologue I mentioned earlier:

```c
const byte kEntryPrologue[] = {
  // Save the heap in rsi, our global heap pointer
  // mov rsi, rdi
  kRexPrefix, 0x89, 0xfe,
};
```

Let's once more add an entry to `Compile_call`.

```c
int Compile_call(Buffer *buf, ASTNode *callable, ASTNode *args,
                 word stack_index, Env *varenv) {
  if (AST_is_symbol(callable)) {
    // ...
    if (AST_symbol_matches(callable, "cons")) {
      return Compile_cons(buf, /*car=*/operand1(args), /*cdr=*/operand2(args),
                          stack_index, varenv);
    }
    // ...
  }
}
```

We don't *really* need to add a whole new function for `cons` since we're not
doing structural recursion on the parameters or anything, but `Compile_call`
just keeps getting bigger and this helps keep it smaller.

`Compile_cons` is pretty much exactly what I described above. I pulled out
`rsi` into `kHeapPointer` so that we can change it later if we need to.

```c
const Register kHeapPointer = kRsi;

int Compile_cons(Buffer *buf, ASTNode *car, ASTNode *cdr,
                 int stack_index, Env *varenv) {
  // Compile and store car on the stack
  _(Compile_expr(buf, car, stack_index, varenv));
  Emit_store_reg_indirect(buf,
                          /*dst=*/Ind(kRbp, stack_index),
                          /*src=*/kRax);
  // Compile and store cdr
  _(Compile_expr(buf, cdr, stack_index - kWordSize, varenv));
  Emit_store_reg_indirect(buf, /*dst=*/Ind(kHeapPointer, kCdrOffset),
                          /*src=*/kRax);
  // Fetch car and store in the heap
  Emit_load_reg_indirect(buf, /*dst=*/kRax, /*src=*/Ind(kRbp, stack_index));
  Emit_store_reg_indirect(buf, /*dst=*/Ind(kHeapPointer, kCarOffset),
                          /*src=*/kRax);
  // Store tagged pointer in rax
  Emit_mov_reg_reg(buf, /*dst=*/kRax, /*src=*/kHeapPointer);
  Emit_or_reg_imm8(buf, /*dst=*/kRax, kPairTag);
  // Bump the heap pointer
  Emit_add_reg_imm32(buf, /*dst=*/kHeapPointer, kPairSize);
  return 0;
}
```

~~Note that even though we're compiling two expressions one right after another,
we don't need to bump `stack_index` or anything. This is because we're storing
the results not on the stack but in the pair~~.

As it turns out, we *do* need to store one of the intermediates on the stack
because otherwise we risk overwriting random data in the heap. As [Leonard
Sch&uuml;tz](https://github.com/KCreate) pointed out to me, the previous
version of this code would fail if either the `car` or `cdr` expressions
modified the heap pointer. Thank you for the correction!

As promised, here is the new instruction to move data between registers:

```c
void Emit_mov_reg_reg(Buffer *buf, Register dst, Register src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0x89);
  Buffer_write8(buf, 0xc0 + src * 8 + dst);
}
```

Alright, that's `cons`. Let's implement `car` and `cdr`. These are
extraordinarily short implementations:

```c
int Compile_call(Buffer *buf, ASTNode *callable, ASTNode *args,
                 word stack_index, Env *varenv) {
  if (AST_is_symbol(callable)) {
    // ...
    if (AST_symbol_matches(callable, "car")) {
      _(Compile_expr(buf, operand1(args), stack_index, varenv));
      Emit_load_reg_indirect(buf, /*dst=*/kRax,
                             /*src=*/Ind(kRax, kCarOffset - kPairTag));
      return 0;
    }
    if (AST_symbol_matches(callable, "cdr")) {
      _(Compile_expr(buf, operand1(args), stack_index, varenv));
      Emit_load_reg_indirect(buf, /*dst=*/kRax,
                             /*src=*/Ind(kRax, kCdrOffset - kPairTag));
      return 0;
    }
    // ...
  }
}
```

Both `car` and `cdr` compile their argument and then load from the resulting
address.

That's it. That's the whole implementation! It's kind of nice that now we have
these building blocks, adding new features is not so hard.

### Testing

I've written a couple of tests for this implementation. In order to make this
testing painless, I've also added a new type of test harness that passes the
tests a buffer *and* a heap. I call it --- wait for it --- `RUN_HEAP_TEST`.

Anyway, here's a test that we can allocate pairs. To fully test it, I've added
some helpers for poking at object internals: `Object_pair_car` and
`Object_pair_cdr`. Note that these may be the same as but are not necessarily
the same as the corresponding AST functions. ~~The C compiler could
hypothetically re-order struct elements, I think~~. *Joker_vD* on Hacker News
points out that C compilers are not permitted to re-order elements, but may
insert padding for alignment.

```c
TEST compile_cons(Buffer *buf, uword *heap) {
  ASTNode *node = Reader_read("(cons 1 2)");
  int compile_result = Compile_entry(buf, node);
  ASSERT_EQ(compile_result, 0);
  // clang-format off
  byte expected[] = {
      // mov rax, 0x2
      0x48, 0xc7, 0xc0, 0x04, 0x00, 0x00, 0x00,
      // mov [rbp-8], rax
      0x48, 0x89, 0x45, 0xf8,
      // mov rax, 0x4
      0x48, 0xc7, 0xc0, 0x08, 0x00, 0x00, 0x00,
      // mov [rsi+Cdr], rax
      0x48, 0x89, 0x46, 0x08,
      // mov rax, [rbp-8]
      0x48, 0x8b, 0x45, 0xf8,
      // mov [rsi+Car], rax
      0x48, 0x89, 0x46, 0x00,
      // mov rax, rsi
      0x48, 0x89, 0xf0,
      // or rax, kPairTag
      0x48, 0x83, 0xc8, 0x01,
      // add rsi, 2*kWordSize
      0x48, 0x81, 0xc6, 0x10, 0x00, 0x00, 0x00,
  };
  // clang-format on
  EXPECT_ENTRY_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_entry(buf, heap);
  ASSERT(Object_is_pair(result));
  ASSERT_EQ_FMT(Object_encode_integer(1), Object_pair_car(result), "0x%lx");
  ASSERT_EQ_FMT(Object_encode_integer(2), Object_pair_cdr(result), "0x%lx");
  AST_heap_free(node);
  PASS();
}
```

Here is a test for that tricky nested `cons` case that messed me up originally:

```c
TEST compile_nested_cons(Buffer *buf, uword *heap) {
  ASTNode *node = Reader_read("(cons (cons 1 2) (cons 3 4))");
  int compile_result = Compile_entry(buf, node);
  ASSERT_EQ(compile_result, 0);
  Buffer_make_executable(buf);
  uword result = Testing_execute_entry(buf, heap);
  ASSERT(Object_is_pair(result));
  ASSERT(Object_is_pair(Object_pair_car(result)));
  ASSERT_EQ_FMT(Object_encode_integer(1),
                Object_pair_car(Object_pair_car(result)), "0x%lx");
  ASSERT_EQ_FMT(Object_encode_integer(2),
                Object_pair_cdr(Object_pair_car(result)), "0x%lx");
  ASSERT(Object_is_pair(Object_pair_cdr(result)));
  ASSERT_EQ_FMT(Object_encode_integer(3),
                Object_pair_car(Object_pair_cdr(result)), "0x%lx");
  ASSERT_EQ_FMT(Object_encode_integer(4),
                Object_pair_cdr(Object_pair_cdr(result)), "0x%lx");
  AST_heap_free(node);
  PASS();
}
```

Here's a test for reading the `car` of a pair. The test for `cdr` is so similar
I will not include it here.

```c
TEST compile_car(Buffer *buf, uword *heap) {
  ASTNode *node = Reader_read("(car (cons 1 2))");
  int compile_result = Compile_entry(buf, node);
  ASSERT_EQ(compile_result, 0);
  // clang-format off
  byte expected[] = {
      // mov rax, 0x2
      0x48, 0xc7, 0xc0, 0x04, 0x00, 0x00, 0x00,
      // mov [rbp-8], rax
      0x48, 0x89, 0x45, 0xf8,
      // mov rax, 0x4
      0x48, 0xc7, 0xc0, 0x08, 0x00, 0x00, 0x00,
      // mov [rsi+Cdr], rax
      0x48, 0x89, 0x46, 0x08,
      // mov rax, [rbp-8]
      0x48, 0x8b, 0x45, 0xf8,
      // mov [rsi+Car], rax
      0x48, 0x89, 0x46, 0x00,
      // mov rax, rsi
      0x48, 0x89, 0xf0,
      // or rax, kPairTag
      0x48, 0x83, 0xc8, 0x01,
      // add rsi, 2*kWordSize
      0x48, 0x81, 0xc6, 0x10, 0x00, 0x00, 0x00,
      // mov rax, [rax-1]
      0x48, 0x8b, 0x40, 0xff,
  };
  // clang-format on
  EXPECT_ENTRY_CONTAINS_CODE(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_entry(buf, heap);
  ASSERT_EQ_FMT(Object_encode_integer(1), result, "0x%lx");
  AST_heap_free(node);
  PASS();
}
```

### Other objects

I didn't cover variable-length objects in this post because I wanted to focus
on the basics of allocating and poking at allocated data structures. Next time,
~~we'll add symbols and strings~~ we'll [learn about instruction encoding]({%
link _posts/2020-10-18-compiling-a-lisp-10.md %}).

{% include compiling_a_lisp_toc.md %}

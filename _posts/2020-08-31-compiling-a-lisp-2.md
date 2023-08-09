---
title: "Compiling a Lisp: Integers"
layout: post
date: 2020-08-31 09:46:00 PDT
description: Compiling Lisp integers to x86-64
og_image: /assets/img/compiling-a-lisp-og-image.png
series: compiling-a-lisp
---

<span data-nosnippet>
*[first](/blog/compiling-a-lisp-0/)* -- *[previous](/blog/compiling-a-lisp-1/)*
</span>

Welcome back to the "Compiling a Lisp" series. Last time we made a small code
execution demo. Today we're going to add the first part of our language:
integer literals. Our programs will look like this:

* `123`
* `-10`
* `0`

But we're not going to put a parser in. That can come later when it gets harder
to manually construct syntax trees.

Also, since implementing full big number support is pretty tricky, we're only
going to support fixed-width numbers. It's entirely possible to then implement
big number support in Lisp after we build out some more features.

### Pointer tagging scheme

Since the integers are always small (less than 64 bits), and we're targeting
x86-64, we can represent the integers as tagged pointers. To read a little more
about that, check out the "Pointer tagging" section of my [Programming
languages resources page](/pl-resources/). Since we'll also represent some
other types of objects as tagged pointers, I'll sketch out a tagging scheme up
front. That way it's easier to reason about than if I draw it out post-by-post.

```
High                                                         Low
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX00  Integer
0000000000000000000000000000000000000000000000000XXXXXXX00001111  Character
00000000000000000000000000000000000000000000000000000000X0011111  Boolean
0000000000000000000000000000000000000000000000000000000000101111  Nil
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX001  Pair
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX010  Vector
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX011  String
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX101  Symbol
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX110  Closure
```

In this diagram, we have some pointer templates composed of `0`s, `1`s, and
`X`s. `0` refers to a 0 bit and `1` refers to a 1 bit.

`X` is a placeholder that refers to payload data for that value. For *immediate
values* --- values whose data are part of the pointer itself --- the `X`s refer
to the data. For heap-allocated objects, it is the pointer address.

It's important to note that we can only accomplish this tagging scheme because
on modern computer systems the lower 3 bits of heap-allocated pointers are `0`
because allocations are *word-aligned* --- meaning that all pointers are
numbers that are multiples of 8. This lets us a) differentiate real pointers
from fake pointers and b) stuff some additional data there in the real
pointers.

These tags let us quickly distinguish objects from one another. Just check the
lower bits:

* Lower 2 bits `00` means integer
* Lower 3 bits `111` means one of the other immediate value types; check the
  lower 7 bits to tell them apart
* For any of the other types, there's a one-to-one mapping of bit pattern in
  the lower 3 bits to the type

This is a choice that Ghuloum made when drawing up the compiler paper. It's
entirely possible to pick your own encoding as long as your encoding *also* has
the property that it's possible to distinguish the type based on the
pointer.[^1]

We're going to be a little clever and use the same encoding scheme inside the
compiler to represent Abstract Syntax Tree (AST) nodes as we are going to use
in the compiled code. I mean, why not? We're going to have to build the
encoding and decoding tools anyway.

### Pointer tagging in practice

We'll start off with integer encoding, since we don't have any other types yet.

```c
#include <assert.h>   // for assert
#include <stdbool.h>  // for bool
#include <stddef.h>   // for NULL
#include <stdint.h>   // for int32_t, etc
#include <string.h>   // for memcpy
#include <sys/mman.h> // for mmap

#include "greatest.h"

// Objects

typedef int64_t word;
typedef uint64_t uword;

const int kBitsPerByte = 8;                        // bits
const int kWordSize = sizeof(word);                // bytes
const int kBitsPerWord = kWordSize * kBitsPerByte; // bits
```

Ignore `greatest.h` --- that is a header-only library I use for lightweight
testing.

`word` and `uword` are type aliases that I will use throughout the codebase to
refer to types of values that fit in registers. It saves us a bunch of typing
and helps keep types consistent.

To avoid some mysterious magical constants, I've also defined helpful names for
the number of bits in a byte (a standard C feature), the number of bytes in a
word, and the number of bits in a word.

```c
const unsigned int kIntegerTag = 0x0;
const unsigned int kIntegerTagMask = 0x3;
const unsigned int kIntegerShift = 2;
const unsigned int kIntegerBits = kBitsPerWord - kIntegerShift;
const word kIntegerMax = (1LL << (kIntegerBits - 1)) - 1;
const word kIntegerMin = -(1LL << (kIntegerBits - 1));

word Object_encode_integer(word value) {
  assert(value < kIntegerMax && "too big");
  assert(value > kIntegerMin && "too small");
  return value << kIntegerShift;
}

// End Objects
```

As we saw above, integers can be fit inside pointers by shifting them two bits
to the left. We have this handy-dandy function, `Object_encode_integer`, for
that.

I've added some bounds checks to make sure we don't accidentally mangle the
values coming in. If the number we're trying to encode is too big or too small,
shifting it left by 2 bits will chop off the left end.

This function is pretty low-level. It doesn't add any new type information (it
returns a `word`, just as it takes a `word`). It's meant to be a utility
function inside the compiler. We'll add another function in a moment that
builds on top of this one to make ASTs.

### Syntax trees

While we could pass around `word`s all day and try really hard to keep the
boundary between integral values and pointer values straight, I don't much
fancy that. I like my type in formation, thank you very much. So we're going to
add a thin veneer over the object encoding that both gives us some nicer type
APIs and gives the C compiler some hints about when we've already encoded an
object.

```c
// AST

struct ASTNode;
typedef struct ASTNode ASTNode;

ASTNode *AST_new_integer(word value) {
  return (ASTNode *)Object_encode_integer(value);
}

bool AST_is_integer(ASTNode *node) {
  return ((word)node & kIntegerTagMask) == kIntegerTag;
}

word AST_get_integer(ASTNode *node) { return (word)node >> kIntegerShift; }

// End AST
```

We'll use these functions pretty heavily in the compiler, especially as we add
more datatypes.

### An expandable byte buffer

Now that we can manually build programs, let's get cracking writing our
buffers. We have to emit the machine code to somewhere, after all. Remember the
`mmap`/`memcpy` stuff from last time? We're going to wrap those in some
easier-to-remember APIs.

```c
// Buffer

typedef unsigned char byte;

typedef enum {
  kWritable,
  kExecutable,
} BufferState;

typedef struct {
  byte *address;
  BufferState state;
  size_t len;
  size_t capacity;
} Buffer;

byte *Buffer_alloc_writable(size_t capacity) {
  byte *result = mmap(/*addr=*/NULL, /*length=*/capacity,
                      /*prot=*/PROT_READ | PROT_WRITE,
                      /*flags=*/MAP_ANONYMOUS | MAP_PRIVATE,
                      /*filedes=*/-1, /*offset=*/0);
  assert(result != MAP_FAILED);
  return result;
}

void Buffer_init(Buffer *result, size_t capacity) {
  result->address = Buffer_alloc_writable(capacity);
  assert(result->address != MAP_FAILED);
  result->state = kWritable;
  result->len = 0;
  result->capacity = capacity;
}

void Buffer_deinit(Buffer *buf) {
  munmap(buf->address, buf->capacity);
  buf->address = NULL;
  buf->len = 0;
  buf->capacity = 0;
}

int Buffer_make_executable(Buffer *buf) {
  int result = mprotect(buf->address, buf->len, PROT_EXEC);
  buf->state = kExecutable;
  return result;
}
```

These functions are good building blocks for creating and destroying buffers.
They abstract away some of the fiddly parameters and add runtime checks.

We still need to write into the buffer at some point, though, and we're not
going to `memcpy` whole blocks in. So let's add some APIs for incremental
writing.

```c
byte Buffer_at8(Buffer *buf, size_t pos) { return buf->address[pos]; }

void Buffer_at_put8(Buffer *buf, size_t pos, byte b) { buf->address[pos] = b; }
```

This `Buffer_at_put8` is the building block of the rest of the compiler. Every
write will go through this function. But notice that it is pretty low-level; it
does not do any bounds checks and it does not advance the current position in
the buffer. So let's add some more functions to do that...

```c
word max(word left, word right) { return left > right ? left : right; }

void Buffer_ensure_capacity(Buffer *buf, word additional_capacity) {
  if (buf->len + additional_capacity <= buf->capacity) {
    return;
  }
  word new_capacity =
      max(buf->capacity * 2, buf->capacity + additional_capacity);
  byte *address = Buffer_alloc_writable(new_capacity);
  memcpy(address, buf->address, buf->len);
  int result = munmap(buf->address, buf->capacity);
  assert(result == 0 && "munmap failed");
  buf->address = address;
  buf->capacity = new_capacity;
}

void Buffer_write8(Buffer *buf, byte b) {
  Buffer_ensure_capacity(buf, sizeof b);
  Buffer_at_put8(buf, buf->len++, b);
}

void Buffer_write32(Buffer *buf, int32_t value) {
  for (size_t i = 0; i < sizeof value; i++) {
    Buffer_write8(buf, (value >> (i * kBitsPerByte)) & 0xff);
  }
}

// End Buffer
```

With the addition of `Buffer_ensure_capacity`, `Buffer_write8`, and
`Buffer_write32`, we can start putting together functions to emit x86-64
instructions. I added both `write8` and `write32` because we'll need to both
emit single bytes and 32-bit immediate integer values. The helper function
ensures that we don't need to think about endian-ness every single time we emit
a 32-bit value.

### Emitting instructions

There are a couple ways we could write an assembler:

* Emit binary directly in the compiler, with comments
* Make a table of all the possible encodings of the instructions we want
  (meaning `mov eax, 1` and `mov ecx, 1` are distinct, for example) and fetch
  chunks of bytes from there
* Use some encoding logic to make re-usable building blocks

I chose to go with the last option, though I've seen all three while looking
for a nice C assembler library. It allows us to write code like
`Emit_mov_reg_imm32(buf, Rcx, 123)`, which if you ask me, looks fairly similar
to `mov rcx, 123`.

If we were writing C++ we could get really clever with operator overloading...
or we could not.

Note that I did *not* make up this encoding logic. This is a common phenomenon
in instruction sets and it helps in decoding (for the hardware) and encoding
(for the compilers).

```c
// Emit

typedef enum {
  kRax = 0,
  kRcx,
  kRdx,
  kRbx,
  kRsp,
  kRbp,
  kRsi,
  kRdi,
} Register;

static const byte kRexPrefix = 0x48;

void Emit_mov_reg_imm32(Buffer *buf, Register dst, int32_t src) {
  Buffer_write8(buf, kRexPrefix);
  Buffer_write8(buf, 0xc7);
  Buffer_write8(buf, 0xc0 + dst);
  Buffer_write32(buf, src);
}

void Emit_ret(Buffer *buf) { Buffer_write8(buf, 0xc3); }

// End Emit
```

Boom. Two instructions. One `mov`, one `ret`. The REX prefix is used in x86-64
to denote that the following instruction, which might have been decoded as
something else in x86-32, means something different in 64-bit mode.

In this particular `mov`'s case, it is the difference between `mov eax, IMM`
and `mov rax, IMM`.

### Compiling our first program

Now that we can emit instructions, it's time to choose what instructions to
emit based on the input program. We have a very restricted set of input
programs (yes, several billion of them, if you're being persnickety about the
range of possible integers) so the implementation is short and sweet.

If we see a literal integer, encode it and put it in `rax`. Done.

```c
// Compile

int Compile_expr(Buffer *buf, ASTNode *node) {
  if (AST_is_integer(node)) {
    word value = AST_get_integer(node);
    Emit_mov_reg_imm32(buf, kRax, Object_encode_integer(value));
    return 0;
  }
  assert(0 && "unexpected node type");
}

int Compile_function(Buffer *buf, ASTNode *node) {
  int result = Compile_expr(buf, node);
  if (result != 0) {
    return result;
  }
  Emit_ret(buf);
  return 0;
}

// End Compile
```

I make a distinction between `expr` and `function` because we don't *always*
want to `ret`. We only want to `ret` the result of a function body, which might
be composed of several nested expressions. This divide will become clearer as
we add more expression types.

### Making sure it works

Our compiler is all well and good, but it's notably more complicated than the
mini JIT demo from the last post. It's one thing to test *that* by manually
checking the return code of `main`, but I think we should have some regression
tests to keep us honest as we go forth and break things.

For that, I've written some testing utilities to help check that we generated
the right code, and also to execute the JITed code and return the result.

```c
typedef int (*JitFunction)();

// Testing

#define EXPECT_EQUALS_BYTES(buf, arr)                                          \
  ASSERT_MEM_EQ(arr, (buf)->address, sizeof arr)

word Testing_execute_expr(Buffer *buf) {
  assert(buf != NULL);
  assert(buf->address != NULL);
  assert(buf->state == kExecutable);
  // The pointer-pointer cast is allowed but the underlying
  // data-to-function-pointer back-and-forth is only guaranteed to work on
  // POSIX systems (because of eg dlsym).
  JitFunction function = *(JitFunction *)(&buf->address);
  return function();
}

// End Testing
```

`ASSERT_MEM_EQ` will check the generated code and point out any differences if
it finds them. Even though this only prints out hex representations of the
generated code, it's very helpful. I often paste unexpected output into `rasm2`
(part of the radare2 suite), Cutter (also part of the radare2 suite), or [this
online disassembler](https://defuse.ca/online-x86-assembler.htm). If the
instructions look super unfamiliar, it means we messed up the encoding!

Since we have our utilities, we're going to use the `greatest.h` testing API to
write some unit tests for our compiler and compiler utilities.

```c
// Tests

TEST encode_positive_integer(void) {
  ASSERT_EQ(0x0, Object_encode_integer(0));
  ASSERT_EQ(0x4, Object_encode_integer(1));
  ASSERT_EQ(0x28, Object_encode_integer(10));
  PASS();
}

TEST encode_negative_integer(void) {
  ASSERT_EQ(0x0, Object_encode_integer(0));
  ASSERT_EQ((word)0xfffffffffffffffc, Object_encode_integer(-1));
  ASSERT_EQ((word)0xffffffffffffffd8, Object_encode_integer(-10));
  PASS();
}

TEST buffer_write8_increases_length(void) {
  Buffer buf;
  Buffer_init(&buf, 5);
  ASSERT_EQ(buf.len, 0);
  Buffer_write8(&buf, 0xdb);
  ASSERT_EQ(Buffer_at8(&buf, 0), 0xdb);
  ASSERT_EQ(buf.len, 1);
  Buffer_deinit(&buf);
  PASS();
}

TEST buffer_write8_expands_buffer(void) {
  Buffer buf;
  Buffer_init(&buf, 1);
  ASSERT_EQ(buf.capacity, 1);
  ASSERT_EQ(buf.len, 0);
  Buffer_write8(&buf, 0xdb);
  Buffer_write8(&buf, 0xef);
  ASSERT(buf.capacity > 1);
  ASSERT_EQ(buf.len, 2);
  Buffer_deinit(&buf);
  PASS();
}

TEST buffer_write32_expands_buffer(void) {
  Buffer buf;
  Buffer_init(&buf, 1);
  ASSERT_EQ(buf.capacity, 1);
  ASSERT_EQ(buf.len, 0);
  Buffer_write32(&buf, 0xdeadbeef);
  ASSERT(buf.capacity > 1);
  ASSERT_EQ(buf.len, 4);
  Buffer_deinit(&buf);
  PASS();
}

TEST buffer_write32_writes_little_endian(void) {
  Buffer buf;
  Buffer_init(&buf, 4);
  Buffer_write32(&buf, 0xdeadbeef);
  ASSERT_EQ(Buffer_at8(&buf, 0), 0xef);
  ASSERT_EQ(Buffer_at8(&buf, 1), 0xbe);
  ASSERT_EQ(Buffer_at8(&buf, 2), 0xad);
  ASSERT_EQ(Buffer_at8(&buf, 3), 0xde);
  Buffer_deinit(&buf);
  PASS();
}


TEST compile_positive_integer(void) {
  word value = 123;
  ASTNode *node = AST_new_integer(value);
  Buffer buf;
  Buffer_init(&buf, 10);
  int compile_result = Compile_function(&buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov eax, imm(123); ret
  byte expected[] = {0x48, 0xc7, 0xc0, 0xec, 0x01, 0x00, 0x00, 0xc3};
  EXPECT_EQUALS_BYTES(&buf, expected);
  Buffer_make_executable(&buf);
  word result = Testing_execute_expr(&buf);
  ASSERT_EQ(result, Object_encode_integer(value));
  PASS();
}

TEST compile_negative_integer(void) {
  word value = -123;
  ASTNode *node = AST_new_integer(value);
  Buffer buf;
  Buffer_init(&buf, 100);
  int compile_result = Compile_function(&buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov eax, imm(-123); ret
  byte expected[] = {0x48, 0xc7, 0xc0, 0x14, 0xfe, 0xff, 0xff, 0xc3};
  EXPECT_EQUALS_BYTES(&buf, expected);
  Buffer_make_executable(&buf);
  word result = Testing_execute_expr(&buf);
  ASSERT_EQ(result, Object_encode_integer(value));
  PASS();
}

SUITE(object_tests) {
  RUN_TEST(encode_positive_integer);
  RUN_TEST(encode_negative_integer);
}

SUITE(buffer_tests) {
  RUN_TEST(buffer_write8_increases_length);
  RUN_TEST(buffer_write8_expands_buffer);
  RUN_TEST(buffer_write32_expands_buffer);
  RUN_TEST(buffer_write32_writes_little_endian);
}

SUITE(compiler_tests) {
  RUN_TEST(compile_positive_integer);
  RUN_TEST(compile_negative_integer);
}

// End Tests

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
  GREATEST_MAIN_BEGIN();
  RUN_SUITE(object_tests);
  RUN_SUITE(buffer_tests);
  RUN_SUITE(compiler_tests);
  GREATEST_MAIN_END();
}
```

These tests pass, at least for me. And no Valgrind errors, either! The full
source for this post can be put together by putting together the individual
code snippets back to back, in order. I recommend following along and typing it
manually, to get the full educational experience, but if you must copy and
paste it should still work. :)

If you want to convince yourself the tests work, modify the values we're
checking against in some places. Then you'll see the test fail. Never trust a
test suite that you haven't seen fail... it might not be running the tests!

I think there is also a way to use `greatest.h` to do setup and teardown so we
don't have to do all that buffer machinery, but I haven't figured out an
ergonomic way to do that yet.

Next time on Dragon Ball Z, we'll [compile some other immediate constants]({%
link _posts/2020-09-02-compiling-a-lisp-3.md %}).

{% include compiling_a_lisp_toc.md %}

[^1]: Actually, you can get away with a scheme that only plays games with
      pointer tagging for immediate objects, and uses a header as part of the
      heap-allocated object to encode additional information about the type,
      the length, etc. This is what runtimes like the JVM do.

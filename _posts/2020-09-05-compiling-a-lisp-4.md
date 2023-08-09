---
title: "Compiling a Lisp: Primitive unary functions"
layout: post
date: 2020-09-05 14:00:00 PDT
description: Compiling Lisp primitive unary functions to x86-64
og_image: /assets/img/compiling-a-lisp-og-image.png
series: compiling-a-lisp
---

<span data-nosnippet>
*[first](/blog/compiling-a-lisp-0/)* -- *[previous](/blog/compiling-a-lisp-3/)*
</span>

Welcome back to the "Compiling a Lisp" series. Last time, we finished adding
the rest of the constants as tagged pointer immediates. Since it's not very
useful to have only values (no way to operate on them), we're going to add some
*primitive unary functions*.

"Primitive" means here that they are built into the compiler, so we won't
actually compile the call to an assembly procedure call. This is also called a
*compiler intrinsic*. "Unary" means the functions will take only one argument.
"Function" is a bit of a misnomer because these functions won't be real values
that you can pass around as variables. You'll only be able to use them as
literal names in calls.

Though we're still not adding a reader/parser, we can imagine the syntax for
this looks like the following:

```common-lisp
(integer? (integer->char (add1 96)))
```

Today we also tackle *nested* function calls and subexpressions.

Adding function calls will require adding a new compiler datastructure, an
addition to the AST, but *not* to the compiled code. The compiled code will
still only know about the immediate types.

Ghuloum proposes we add the following functions:

* `add1`, which takes an integer and adds 1 to it
* `sub1`, which takes an integer and subtracts 1 from it
* `integer->char`, which takes an integer and converts it into a character
  (like `chr` in Python)
* `char->integer`, which takes a character and converts it into an integer
  (like `ord` in Python)
* `null?`, which takes an object and returns true if it is `nil` and false
  otherwise
* `zero?`, which takes an object and returns true if it is `0` and false
  otherwise
* `not`, which takes an object and returns true if it is `false` and false
  otherwise
* `integer?`, which takes an object and returns true if it is an integer and
  false otherwise
* `bool?`, which takes an object and returns true if it is a boolean and
  false otherwise

The functions `add1`, `sub1`, and the `char`/`integer` conversion functions
will be our first real experience dealing with object encoding in the compiled
code. What fun!

The implementations for `null?`, `zero?`, `not`, `integer`?, and `bool?` are so
similar that I am only going to reproduce one or two in this post. The rest
will be visible at
[assets/code/lisp/compiling-unary.c](https://github.com/tekknolagi/tekknolagi.github.com/blob/3d956655e48a124e828a8847282caa71b1c8b63f/assets/code/lisp/compiling-unary.c).

In order to implement these functions, we'll also need some more instructions
than `mov` and `ret`. Today we'll add:

* `add`
* `sub`
* `shl`
* `shr`
* `or`
* `and`
* `cmp`
* `setcc`

Because the implementations of `shl`, `shr`, `or`, and `and` are so
straightforward --- just like `mov`, really --- I'll also omit them from the
post. The implementations of `add`, `sub`, `cmp`, and `setcc` are more
interesting.

### The fundamental data structure of Lisp

Pairs, also called [cons cells](https://en.wikipedia.org/wiki/Cons),
two-tuples, and probably other things too, are the fundamental data structure
of Lisp. At least the original Lisp. Nowadays we have fancy structures like
vectors, too.

Pairs are a container for precisely two other objects. I'll call them `car` and
`cdr` for historical[^1] and consistency reasons, but you can call them
whatever you like. Regardless of name, they could be represented as a C struct
like this:

```c
typedef struct Pair {
  ASTNode *car;
  ASTNode *cdr;
} Pair;
```

This is useful for holding pairs of objects (think coordinates, complex
numbers, ...) but it is also incredibly useful for making linked lists. Linked
lists in Lisp are comprised of a `car` holding an object and the `cdr` holding
another list. Eventually the last `cdr` holds `nil`, signifying the end of the
list. Take a look at this handy diagram.

<figure style="display: block; margin: 0 auto; max-width: 400px;">
  <img class="svg" style="max-width: 400px;" src="/assets/img/lisp/03_lists_cons.svg" />
  <figcaption>Fig. 1 - Cons cell list, courtesy of Wikipedia.</figcaption>
</figure>

This represents the list `(list 42 69 613)`, which can also be denoted `(cons
42 (cons 69 (cons 613 nil)))`.

We'll use these lists to represent the syntax trees for Lisp, so we'll need to
implement pairs to compile list programs.

### Implementing pairs

In previous posts we implemented the immediate types the same way in the
compiler and in the compiled code. I originally wrote this post doing the same
thing: manually laying out object offsets myself, reading and writing from
objects manually. The motivation was to get you familiar with the memory
layout in the compiled code, but ultimately it ended up being too much content
too fast. We'll get to memory layouts when we start allocating pairs in the
compiled code.

In the compiler we're going to use C structs instead of manual memory layout.
This makes the code a little bit easier to read. We'll still tag the pointers,
though.

```c
const unsigned int kPairTag = 0x1;        // 0b001
const uword kHeapTagMask = ((uword)0x7);  // 0b000...0111
const uword kHeapPtrMask = ~kHeapTagMask; // 0b1111...1000
```

This adds the pair tag and some masks. As we noted in the previous posts, the
heap object tags are all in the lowest three bits of the pointer. We can mask
those out using this handy utility function.

```c
uword Object_address(void *obj) { return (uword)obj & kHeapPtrMask; }
```

We'll need to use this whenever we want to actually access a struct member.
Speaking of struct members, here's the definition of `Pair`:

```c
typedef struct Pair {
  ASTNode *car;
  ASTNode *cdr;
} Pair;
```

And here are some functions for allocating and manipulating the `Pair` struct,
to keep the implementation details hidden:

```c
ASTNode *AST_heap_alloc(unsigned char tag, uword size) {
  // Initialize to 0
  uword address = (uword)calloc(size, 1);
  return (ASTNode *)(address | tag);
}

void AST_pair_set_car(ASTNode *node, ASTNode *car);
void AST_pair_set_cdr(ASTNode *node, ASTNode *cdr);

ASTNode *AST_new_pair(ASTNode *car, ASTNode *cdr) {
  ASTNode *node = AST_heap_alloc(kPairTag, sizeof(Pair));
  AST_pair_set_car(node, car);
  AST_pair_set_cdr(node, cdr);
  return node;
}

bool AST_is_pair(ASTNode *node) {
  return ((uword)node & kHeapTagMask) == kPairTag;
}

Pair *AST_as_pair(ASTNode *node) {
  assert(AST_is_pair(node));
  return (Pair *)Object_address(node);
}

ASTNode *AST_pair_car(ASTNode *node) { return AST_as_pair(node)->car; }

void AST_pair_set_car(ASTNode *node, ASTNode *car) {
  AST_as_pair(node)->car = car;
}

ASTNode *AST_pair_cdr(ASTNode *node) { return AST_as_pair(node)->cdr; }

void AST_pair_set_cdr(ASTNode *node, ASTNode *cdr) {
  AST_as_pair(node)->cdr = cdr;
}
```

There a couple important things to note.

First, `AST_heap_alloc` very intentionally zeroes out the memory it allocates.
If the members were left uninitialized, it might be possible to read off a
struct member that had an invalid pointer in `car` or `cdr`. If we
zero-initialize it, the member pointers represent the object `0` by default.
Nothing will crash.

Second, we keep moving our `ASTNode` pointers through `AST_as_pair`. This
function has two purposes: catch invalid uses (via the `assert` that the object
is indeed a `Pair`) and also mask out the lower bits. Otherwise we'd have to do
the masking in every operation individually.

Third, I abstracted out the `AST_heap_alloc` so we don't expose the `calloc`
function everywhere. This allows us to later swap out the allocator for
something more intelligent, like a bump allocator, an arena allocator, etc.

And since memory allocated must eventually be freed, there's a freeing function
too:

```c
void AST_heap_free(ASTNode *node) {
  if (!AST_is_heap_object(node)) {
    return;
  }
  if (AST_is_pair(node)) {
    AST_heap_free(AST_pair_car(node));
    AST_heap_free(AST_pair_cdr(node));
  }
  free((void *)Object_address(node));
}
```

This assumes that each `ASTNode*` owns the references to all of its members. So
**don't borrow references** to share between objects. If you need to store a
reference to an object, **make sure you own it**. Otherwise you'll get a double
free. In practice this shouldn't bite us too much because each program is one
big tree.

### Implementing symbols

We also need symbols! I mean, we could try mapping all the functions we need to
integers, but that wouldn't be very fun. Who wants to try and debug a program
crashing on `function#67`? Not me. So let's add a datatype that can represent
names of things.

As above, we'll need to tag the pointers.

```c
const unsigned int kSymbolTag = 0x5;      // 0b101
```

And then our struct definition.

```c
typedef struct Symbol {
  word length;
  char cstr[];
} Symbol;
```

I've chosen this variable-length object representation because it's similar to
how we're going to allocate symbols in assembly and the mechanism in C isn't so
gnarly. This struct indicates that the memory layout of a `Symbol` is a length
field immediately followed by that number of bytes in memory. Note that having
this variable array in a struct is a **C99 feature**.

> If you don't have C99 or don't like this implementation, that's fine. Just
> store a `char*` and allocate another object for that string.

> You could also opt to not store the length *at all* and instead NUL-terminate
> it. This has the advantage of not dealing with variable-length arrays (it's
> just a tagged `char*`) but has the disadvantage of an `O(n)` length lookup.

Now we can add our `Symbol` allocator:

```c
Symbol *AST_as_symbol(ASTNode *node);

ASTNode *AST_new_symbol(const char *str) {
  word data_length = strlen(str) + 1; // for NUL
  ASTNode *node = AST_heap_alloc(kSymbolTag, sizeof(Symbol) + data_length);
  Symbol *s = AST_as_symbol(node);
  s->length = data_length;
  memcpy(s->cstr, str, data_length);
  return node;
}
```

See how we have to manually specify the size we want. It's a little fussy, but
it works.

Storing the NUL byte or not is up to you. It saves one byte per string if you
don't, but it makes printing out strings in the debugger a bit of a pain since
you can't just treat them like normal C strings.

> Some Lisp implementations use a *symbol table* to ensure that symbols
> allocated with equivalent C-string values return *the same pointer*. This
> allows the implementations to test for symbol equality by testing pointer
> equality. I think we can sacrifice a bit of memory and runtime speed for
> implementation simplicity, so I'm not going to do that.

Let's add the rest of the utility functions:

```c
bool AST_is_symbol(ASTNode *node) {
  return ((uword)node & kHeapTagMask) == kSymbolTag;
}

Symbol *AST_as_symbol(ASTNode *node) {
  assert(AST_is_symbol(node));
  return (Symbol *)Object_address(node);
}

const char *AST_symbol_cstr(ASTNode *node) {
  return (const char *)AST_as_symbol(node)->cstr;
}

bool AST_symbol_matches(ASTNode *node, const char *cstr) {
  return strcmp(AST_symbol_cstr(node), cstr) == 0;
}
```

Now we can represent names.

### Representing function calls

We're going to represent function calls as lists. That means that the following program:

```common-lisp
(add1 5)
```

can be represented by the following C program:

```c
Pair *args = AST_new_pair(AST_new_integer(5), AST_nil());
Pair *program = AST_new_pair(AST_new_symbol("add1"), args);
```

This is a little wordy. We can make some utilities to trim the length down.

```c
ASTNode *list1(ASTNode *item0) {
  return AST_new_pair(item0, AST_nil());
}

ASTNode *list2(ASTNode *item0, ASTNode *item1) {
  return AST_new_pair(item0, list1(item1));
}

ASTNode *new_unary_call(const char *name, ASTNode *arg) {
  return list2(AST_new_symbol(name), arg);
}
```

And now we can represent the program as:

```c
list2(AST_new_symbol("add1"), AST_new_integer(5));
// or, shorter,
new_unary_call("add1", AST_new_integer(5));
```

This is great news because we'll be adding many tests today.

### Compiling primitive unary function calls

Whew. We've built up all these data structures and tagged pointers and whatnot
but haven't actually done anything with them yet. Let's get to the compilers
part of the compilers series, please!

First, we have to revisit `Compile_expr` and add another case. If we see a pair
in an expression, then that indicates a call.

```c
int Compile_expr(Buffer *buf, ASTNode *node) {
  // Tests for the immediates ...
  if (AST_is_pair(node)) {
    return Compile_call(buf, AST_pair_car(node), AST_pair_cdr(node));
  }
  assert(0 && "unexpected node type");
}
```

I took the liberty of separating out the callable and the args so that the
`Compile_call` function has less to deal with.

We're only supporting primitive unary function calls today, which means that we
have a very limited pattern of what is accepted by the compiler. `(add1 5)` is
ok. `(add1 (add1 5))` is ok. `(blargle 5)` is not, because the `blargle` isn't
on the list above. `((foo) 1)` is not, because the thing being called is not a
symbol.

```c
int Compile_call(Buffer *buf, ASTNode *callable, ASTNode *args) {
  assert(AST_pair_cdr(args) == AST_nil() &&
         "only unary function calls supported");
  if (AST_is_symbol(callable)) {
    // Switch on the different primitives here...
  }
  assert(0 && "unexpected call type");
}
```

`Compile_call` should look at what symbol it is, and depending on which symbol
it is, emit different code. The overall pattern will look like this, though:

* Compile the argument --- the result is stored in `rax`
* Do something to `rax`

Let's start with `add1` since it's the most straightforward.

```c
    if (AST_symbol_matches(callable, "add1")) {
      _(Compile_expr(buf, operand1(args)));
      Emit_add_reg_imm32(buf, kRax, Object_encode_integer(1));
      return 0;
    }
```

If we see `add1`, compile the argument (as above). Then, add 1 to `rax`. Note
that we're not just adding the literal `1`, though. We're adding the object
representation of 1, ie `1 << 2`. Think about why! When you have an idea, click
the footnote.[^2]

If you're wondering what the underscore (`_`) function is, it's a macro that I
made to test the return value of the compile expression and return if there was
an error. We don't have any non-aborting error cases just yet, but I got tired
of writing `if (result != 0) return result;` over and over again.

Note that there is no runtime error checking. Our compiler will allow `(add1
nil)` to slip through and mangle the pointer. This isn't ideal, but we don't
have the facilities for error reporting just yet.

`sub1` is similar to `add1`, except it uses the `sub` instruction. You could
also just use `add` with the immediate representation of `-1`.

`integer->char` is different. We have to change the tag of the object. In order
to do that, we shift the integer left and then drop the character tag onto it.
This is made simple by integers having a `0b00` tag (nothing to mask out).

Here's a small diagram showing the transitions when converting `97` to `'a'`:

```
High                                                           Low
0000000000000000000000000000000000000000000000000000000[1100001]00  Integer
0000000000000000000000000000000000000000000000000[1100001]00000000  Shifted
0000000000000000000000000000000000000000000000000[1100001]00001111  Character
```

where the number in enclosed in `[`brackets`]` is `97`. And here's the code to
emit assembly that does just that:

```c
    if (AST_symbol_matches(callable, "integer->char")) {
      _(Compile_expr(buf, operand1(args)));
      Emit_shl_reg_imm8(buf, kRax, kCharShift - kIntegerShift);
      Emit_or_reg_imm8(buf, kRax, kCharTag);
      return 0;
    }
```

Note that we're not shifting left by the full amount. We're only shifting by
the difference, since integers are already two bits shifted.

`char->integer` is similar, except it's just a `shr`. Once the value is shifted
right, the char tag gets dropped off the end, so there's no need to mask it
out.

`nil?` is our first primitive with ~ exciting assembly instructions ~. We get
to use `cmp` *and* `setcc`. The basic idea is:

* Compare (this means do a subtraction) what's in `rax` and `nil`
* Set `rax` to 0
* If they're equal (this means the result was 0), set `al` to 1
* Shift left and tag it with the bool tag

`al` is the name for the lower 8 bits of `rax`. There's also `ah` (for the
next 8 bits, but not the highest bits), `cl`/`ch`, etc.

```c
    if (AST_symbol_matches(callable, "nil?")) {
      _(Compile_expr(buf, operand1(args)));
      Emit_cmp_reg_imm32(buf, kRax, Object_nil());
      Emit_mov_reg_imm32(buf, kRax, 0);
      Emit_setcc_imm8(buf, kEqual, kAl);
      Emit_shl_reg_imm8(buf, kRax, kBoolShift);
      Emit_or_reg_imm8(buf, kRax, kBoolTag);
      return 0;
    }
```

The `cmp` leaves a bit set (ZF) in the flags register, which `setcc` then
checks. `setcc`, by the way, is the name for the group of instructions that set
a register if some condition happened. It took me a long time to realize that
since people normally write `sete` or `setnz` or something. And *cc* means
"condition code".

If you want to simplify your life --- we're going to do a lot of comparisons
today -- we can extract that into a function that compares `rax` with some
immediate value, and then refactor `Compile_call` to call that.

```c
void Compile_compare_imm32(Buffer *buf, int32_t value) {
  Emit_cmp_reg_imm32(buf, kRax, value);
  Emit_mov_reg_imm32(buf, kRax, 0);
  Emit_setcc_imm8(buf, kEqual, kAl);
  Emit_shl_reg_imm8(buf, kRax, kBoolShift);
  Emit_or_reg_imm8(buf, kRax, kBoolTag);
}
```

Let's also poke at the implementations of `cmp` and `setcc`, since they involve
some fun instruction encoding.

`cmp`, as it turns out, has a short-path when the register it's comparing
against is `rax`. This means we get to save one (1) whole byte if we want to!

```c
void Emit_cmp_reg_imm32(Buffer *buf, Register left, int32_t right) {
  Buffer_write8(buf, kRexPrefix);
  if (left == kRax) {
    // Optimization: cmp rax, {imm32} can either be encoded as 3d {imm32} or 81
    // f8 {imm32}.
    Buffer_write8(buf, 0x3d);
  } else {
    Buffer_write8(buf, 0x81);
    Buffer_write8(buf, 0xf8 + left);
  }
  Buffer_write32(buf, right);
}
```

If you don't want to, just use the `81 f8+` pattern.

For `setcc`, we have to define this new notion of "partial registers" so that
we can encode the instruction properly. We can't re-use `Register` because
there are two partial registers for `rax`. So we add a `PartialRegister`.

```c
typedef enum {
  kAl = 0,
  kCl,
  kDl,
  kBl,
  kAh,
  kCh,
  kDh,
  kBh,
} PartialRegister;
```

And then we can use those in the `setcc` implementation:

```c
void Emit_setcc_imm8(Buffer *buf, Condition cond, PartialRegister dst) {
  Buffer_write8(buf, 0x0f);
  Buffer_write8(buf, 0x90 + cond);
  Buffer_write8(buf, 0xc0 + dst);
}
```

Again, I didn't come up with this encoding. This is Intel's design.

The `zero?` primitive is much the same as `nil?`, and we can re-use that
`Compile_compare_imm32` function.

```c
    if (AST_symbol_matches(callable, "zero?")) {
      _(Compile_expr(buf, operand1(args)));
      Compile_compare_imm32(buf, Object_encode_integer(0));
      return 0;
    }
```

`not` is more of the same --- compare against `false`.

Now we get to `integer?`. This is similar, but different enough that I'll
reproduce the implementation below. Instead of comparing the whole number in
`rax`, we only want to look at the lowest 2 bits. This can be accomplished by
masking out the other bits, and then doing the comparison. For that, we emit an
`and` first and compare against the tag.

```c
    if (AST_symbol_matches(callable, "integer?")) {
      _(Compile_expr(buf, operand1(args)));
      Emit_and_reg_imm8(buf, kRax, kIntegerTagMask);
      Compile_compare_imm32(buf, kIntegerTag);
      return 0;
    }
```

It's possible to shorten the implementation a little bit because `and` [sets
the zero flag](https://www.felixcloutier.com/x86/and). This means we can skip
the `cmp`. But it's only one instruction and I'm lazy so I'm reusing the
existing infrastructure.

Last, `boolean?` is almost the same as `integer?`.

Boom! Compilers! Let's check our work.

### Testing

I'll only include a couple tests here, since the new tests are a total of 283
lines added and are a little bit repetitive.

First, the simplest test for `add1`.

```c
TEST compile_unary_add1(Buffer *buf) {
  ASTNode *node = new_unary_call("add1", AST_new_integer(123));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov rax, imm(123); add rax, imm(1); ret
  byte expected[] = {0x48, 0xc7, 0xc0, 0xec, 0x01, 0x00, 0x00,
                     0x48, 0x05, 0x04, 0x00, 0x00, 0x00, 0xc3};
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_integer(124));
  AST_heap_free(node);
  PASS();
}
```

Second, a test of nested expressions:

```c
TEST compile_unary_add1_nested(Buffer *buf) {
  ASTNode *node = new_unary_call(
      "add1", new_unary_call("add1", AST_new_integer(123)));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov rax, imm(123); add rax, imm(1); add rax, imm(1); ret
  byte expected[] = {0x48, 0xc7, 0xc0, 0xec, 0x01, 0x00, 0x00,
                     0x48, 0x05, 0x04, 0x00, 0x00, 0x00, 0x48,
                     0x05, 0x04, 0x00, 0x00, 0x00, 0xc3};
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_integer(125));
  AST_heap_free(node);
  PASS();
}
```

Third, the test for `boolean?`.

```c
TEST compile_unary_booleanp_with_non_boolean_returns_false(Buffer *buf) {
  ASTNode *node = new_unary_call("boolean?", AST_new_integer(5));
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // 0:  48 c7 c0 14 00 00 00    mov    rax,0x14
  // 7:  48 83 e0 3f             and    rax,0x3f
  // b:  48 3d 1f 00 00 00       cmp    rax,0x0000001f
  // 11: 48 c7 c0 00 00 00 00    mov    rax,0x0
  // 18: 0f 94 c0                sete   al
  // 1b: 48 c1 e0 07             shl    rax,0x7
  // 1f: 48 83 c8 1f             or     rax,0x1f
  byte expected[] = {0x48, 0xc7, 0xc0, 0x14, 0x00, 0x00, 0x00, 0x48, 0x83,
                     0xe0, 0x3f, 0x48, 0x3d, 0x1f, 0x00, 0x00, 0x00, 0x48,
                     0xc7, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x94, 0xc0,
                     0x48, 0xc1, 0xe0, 0x07, 0x48, 0x83, 0xc8, 0x1f};
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_false());
  AST_heap_free(node);
  PASS();
}
```

I'm getting the fancy disassembly from
[defuse.ca](https://defuse.ca/online-x86-assembler.htm). I include it because
it makes the tests easier for me to read and reason about later. You just have
to make sure the text and the binary representations in the test don't go out
of sync because that can be *very* confusing...

Anyway, that's a wrap for today. Send your comments [on the
elist](https://lists.sr.ht/~max/compiling-lisp)! Next time,
[binary primitives](/blog/compiling-a-lisp-5/).

{% include compiling_a_lisp_toc.md %}

[^1]: There's a long-running dispute about what to call these two objects. The
      original Lisp machine (the IBM 704) had a particular hardware layout that
      led to the creation of the names `car` and `cdr`. Nobody uses this
      hardware anymore, so the names are historical. Some people call them
      `first`/`fst` and `second`/`snd`. Others call them `head`/`hd` and
      `tail`/`tl`. Some people have [other
      ideas](https://twitter.com/iximeow/status/1302065940712927232).

[^2]: If you said "to preserve the tag" or "adding 1 would make it a pair" or
      some variant on that, you're correct! Otherwise, I recommend going back
      to the diagram from the last couple of posts and then writing down binary
      representations of a couple of numbers by hand on a piece of paper.

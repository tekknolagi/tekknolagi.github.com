---
title: "Compiling a Lisp: Booleans, characters, nil"
layout: post
date: 2020-09-02 00:45:00 PDT
description: Compiling other Lisp immediates to x86-64
og_image: /assets/img/compiling-a-lisp-og-image.png
series: compiling-a-lisp
---

<span data-nosnippet>
*[first](/blog/compiling-a-lisp-0/)* -- *[previous](/blog/compiling-a-lisp-2/)*
</span>

Welcome back to the "Compiling a Lisp" series. Last time, we compiled integer
literals. In today's relatively short post, we'll add the rest of the immediate
types. Our programs will look like this:

* `'a'`
* `true`
* `false`
* `nil` or `()`

In addition, since we're not adding too much exciting stuff today, I made
writing tests a little bit easier by adding *fixtures*. Now, if we want, we can
get a pre-made `Buffer` object passed into the test, and then have it destroyed
afterward.

### Encoding

Since we're coming back to the pointer tagging scheme, I've reproduced the
"pointer templates" (I don't think that's a real term) diagram from the last
post below.

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

Notice that we have a pattern among the other immediates (character, boolean,
and nil) -- the lower four bits are all the same, and that sets them apart from
other pointer types.

Also notice that among those immediates, they can be discriminated by the two
bits just above those four:

```
High                                                             Low
0000000000000000000000000000000000000000000000000XXXXXXX00[00][1111]  Character
00000000000000000000000000000000000000000000000000000000X0[01][1111]  Boolean
0000000000000000000000000000000000000000000000000000000000[10][1111]  Nil
```

So a lower four bits of `0b1111` means immediate, and from there `0b00` means
character, `0b01` means boolean, and `0b10` means nil. There's even room to add
another immediate tag pattern (`0b11`) if we like.

Let's add some of the symbolic constants for bit manipulation.

```c
const unsigned int kImmediateTagMask = 0x3f;

const unsigned int kCharTag = 0xf;   // 0b00001111
const unsigned int kCharMask = 0xff; // 0b11111111
const unsigned int kCharShift = 8;

const unsigned int kBoolTag = 0x1f;  // 0b0011111
const unsigned int kBoolMask = 0x80; // 0b10000000
const unsigned int kBoolShift = 7;
```

Notice that we don't have any for nil. That's because nil is a singleton and
has no payload at all. It's just a solitary `0x2f`.

For the others, we need to put the payload alongside the tag, and that requires
a shift and a bitwise `or`. The first operation, the shift, moves the payload
left enough that there's space for a tag, and the `or` adds the tag.

```c
word Object_encode_char(char value) {
  return ((word)value << kCharShift) | kCharTag;
}

char Object_decode_char(word value) {
  return (value >> kCharShift) & kCharMask;
}

word Object_encode_bool(bool value) {
  return ((word)value << kBoolShift) | kBoolTag;
}

bool Object_decode_bool(word value) { return value & kBoolMask; }

word Object_true() { return Object_encode_bool(true); }

word Object_false() { return Object_encode_bool(false); }

word Object_nil() { return 0x2f; }
```

For bool, we've done a little trick. Since we only care if the value is true or
false, instead of doing both a shift *and* mask to decode, we can turn off the
tag bits. The resulting value will be either `0b00000000` for `false` or
`0b10000000` for `true`. Since any non-zero value is truthy in C, we can "cast"
that to a C `bool` by just returning it.

Note that the cast from `char` and `bool` to `word` is necessary because --- as
I learned the hard way, several months ago --- shifting a type left more to the
left than the size has bits is either undefined or implementation-defined
behavior. I can't remember which offhand but the situation went sideways and
left me scratching my head.

I added `Object_true` and `Object_false` because I thought they might come in
handy at some point, but we don't have a use for them now. If you are strongly
against including dead weight code, then feel free to omit them. 

Now let's add some more AST utility functions before we move on to compiling:

```c
bool AST_is_char(ASTNode *node) {
  return ((word)node & kImmediateTagMask) == kCharTag;
}

char AST_get_char(ASTNode *node) { return Object_decode_char((word)node); }

ASTNode *AST_new_char(char value) {
  return (ASTNode *)Object_encode_char(value);
}

bool AST_is_bool(ASTNode *node) {
  return ((word)node & kImmediateTagMask) == kBoolTag;
}

bool AST_get_bool(ASTNode *node) { return Object_decode_bool((word)node); }

ASTNode *AST_new_bool(bool value) {
  return (ASTNode *)Object_encode_bool(value);
}

bool AST_is_nil(ASTNode *node) { return (word)node == Object_nil(); }

ASTNode *AST_nil() { return (ASTNode *)Object_nil(); }
```

Enough talk about object encoding. Let's compile some immediates.

### Compiling

The implementation is much the same as for integers. Check the type, pull out
the payload, move to `rax`.

```c
int Compile_expr(Buffer *buf, ASTNode *node) {
  if (AST_is_integer(node)) {
    word value = AST_get_integer(node);
    Emit_mov_reg_imm32(buf, kRax, Object_encode_integer(value));
    return 0;
  }
  if (AST_is_char(node)) {
    char value = AST_get_char(node);
    Emit_mov_reg_imm32(buf, kRax, Object_encode_char(value));
    return 0;
  }
  if (AST_is_bool(node)) {
    bool value = AST_get_bool(node);
    Emit_mov_reg_imm32(buf, kRax, Object_encode_bool(value));
    return 0;
  }
  if (AST_is_nil(node)) {
    Emit_mov_reg_imm32(buf, kRax, Object_nil());
    return 0;
  }
  assert(0 && "unexpected node type");
}
```

I suppose we could coalesce these by checking if the `node` is any sort of
immediate and then writing the address immediately back with
`Emit_mov_reg_imm32`... but that would be breaking abstractions or something.

### Testing

The testing is also so much the same --- so much so, that I'll only include the
test for compiling characters. The other code is available from
[assets/code/lisp](https://github.com/tekknolagi/tekknolagi.github.com/blob/6a38feeb4dc63a528877a17f576756d36ba985cd/assets/code/lisp/compiling-immediates.c)
if you would like a reference.

```c
TEST compile_char(Buffer *buf) {
  char value = 'a';
  ASTNode *node = AST_new_char(value);
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  // mov eax, imm('a'); ret
  byte expected[] = {0x48, 0xc7, 0xc0, 0x0f, 0x61, 0x00, 0x00, 0xc3};
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  word result = Testing_execute_expr(buf);
  ASSERT_EQ(result, Object_encode_char(value));
  PASS();
}
```

You'll notice that instead of `void`, the function now takes `Buffer*`. This is
part of the new testing fixtures setup that I mentioned earlier. The
implementation is a macro that uses `greatest.h`'s "pass a parameter to your
test" feature. Running a test looks much the same:

```c
RUN_BUFFER_TEST(compile_char);
```

Anyway, that's a wrap for today. Next time we'll [add some unary primitives]({%
link _posts/2020-09-05-compiling-a-lisp-4.md %}) for querying and manipulating
the objects we have already.

{% include compiling_a_lisp_toc.md %}

---
title: "Some tricks from the Scrapscript compiler"
layout: post
date: 2024-07-01
---

[Scrapscript](https://scrapscript.org/) is a small, pure, functional,
content-addressable, network-first programming language.

```
fact 5
. fact =
  | 0 -> 1
  | n -> n * fact (n - 1)
```

My [previous post](/blog/scrapscript-baseline/) talked about the compiler that
Chris and I built. This post is about some tricks that we've added since.

Pretty much all of these tricks are standard operating procedure for language
runtimes (OCaml, MicroPython, Skybison, etc). We didn't invent them.

## Immediate objects

I mentioned that we can encode small integers and some other objects inside
pointers so that we don't have to allocate them at all. In this post, I'll
explain a little bit more how we encode small strings and also now certain
kinds of variants!

The compiler assumes we are compiling to a 64 bit machine[^32-bit]. This means
that with every pointer, we have 8 bytes to play with. We already use some of
the low byte for tagging, and I showed these patterns in the previous post:

[^32-bit]: This isn't a hard constraint and we would only need to do a little
    bit of refactoring to support 32 bit architectures. To simplify the
    development process, though, we're sticking with 64 bits.

```
0bxxx...xx0  // small int
0bxxx...001  // heap object
```

But there are more that we use in the compiler that I did not explain. So here
are the rest of the bit patterns:

```
0b000...00101  // empty list
0b000...00111  // hole
0bxxx...01101  // small string
0b000...01111  // immediate variant
```

Immediate patterns have two constraints:

1. The bit pattern must have the low bit set (to avoid conflicting with small
   integers)
1. The bit pattern must *not* have 001 as the low bits (to avoid conflicting
   with heap objects)

Everything else is fair game. We technically could use as many of the high bits
as we want and build in gigantic amounts of patterns but instead we are going
to reserve those high bits for small strings.

### Small strings

Small strings are pretty neat. The idea is to use 7 bytes for the string data
and one byte for the tag. Since the tag is only 5 bits long, we have 3 bits
left over of the final remaining byte to encode... the length!

```
0bxxx...LLL01101  // small string
```

This all may sound kind of complicated and abstract but see if reading some
Python code helps[^stack-strings]. Instead of generating the full pointer value
in Python code (which would mean the compiler has to have some magic constants
in it), we generate a string of C code:

[^stack-strings]: In fact, I used some similar code to obfuscate programs that
    I wrote for a Windows malware course. Instead of encoding stack strings by
    hand, I wrote a program to generate them for me for use in C programs.
    TODO

```python
def small_string(value_str: str) -> str:
    value = value_str.encode("utf-8")
    length = len(value)
    value_int = int.from_bytes(value, "little")
    return f"(struct object*)(({hex(value_int)}ULL << kBitsPerByte) | ({length}ULL << kImmediateTagBits) | (uword)kSmallStringTag /* {value_str!r} */)"

small_string("")
# (struct object*)((0x0ULL << kBitsPerByte) | (0ULL << kImmediateTagBits) | (uword)kSmallStringTag /* '' */)
small_string("abc")
# (struct object*)((0x636261ULL << kBitsPerByte) | (3ULL << kImmediateTagBits) | (uword)kSmallStringTag /* 'abc' */)
```

Because the encoded version is illegible without knowing ASCII tables in your
head (0x63=="c", 0x62=="b", 0x61=="a", etc), we also helpfully print out the
string representation to aid debugging.

It's stored "backwards" if you print out the pointer MSB first but that's
because we store it little-endian. I don't know if there's a specific reason
for doing this... it's how we did it in Skybison and I kept that the same.

### Variants

As a refresher, variants are kind of like a dynamically typed version of
SML/OCaml variants. Whereas in OCaml and SML they are checked statically and
erased at compile-time, Scrapscript keeps the tags around at run-time. You can
kind of think of it like adding one string's worth of metadata to an object
that you can match on.

```
eval =
| #int_lit x -> x
| #add [left, right] -> left + right
```

Until today, variants were all heap-allocated pairs of tag and value.

```c
struct variant {
  struct gc_obj HEAD;
  size_t tag;
  struct object* value;
};
```

The tag is an index into the tag table which is generated at compile-time and
contains all tags:

```c
enum {
  Tag_foo,
  Tag_bar,
  // ...
};
```

It's kind of unfortunate, because it means that writing something like
`#some_tag [a, b]` allocates an object first for the list and then also for the
tagged wrapper object.

### True and false

## The const heap

## Playing with the compiler

Try running `./compiler.py --compile examples/0_home/factorial.scrap` which
will produce both `output.c` and `a.out`. Then you can run `./a.out` to see the
result of your program.

## Thanks for reading

Well first, play with [the web REPL](https://scrapscript.fly.dev/repl). Then
take a look at [the repo](https://github.com/tekknolagi/scrapscript) and start
contributing! Since we don't have a huge backlog of well-scoped projects just
yet, I recommend posting in the [discourse
group](https://scrapscript.discourse.group/) first to get an idea of what would
be most useful and also interesting to you.

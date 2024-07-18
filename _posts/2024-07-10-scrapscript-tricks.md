---
title: "Some tricks from the Scrapscript compiler"
layout: post
date: 2024-07-10
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
Chris and I built. This post is about some optimization tricks that we've added
since.

Pretty much all of these tricks are standard operating procedure for language
runtimes (OCaml, MicroPython, Objective-C, Skybison, etc). We didn't invent
them.

They're also somewhat limited in scope; the goal was to be able to add as much
as possible to the baseline compiler without making it or the runtime notably
more complicated. A fully-featured optimizing compiler is coming soon&trade;
but not ready yet.

## Immediate objects

In the [last post](/blog/scrapscript-baseline/), I mentioned that we can encode
small integers and some other objects inside pointers so that we don't have to
heap allocate them at all. In this post, I'll explain a little bit more how we
encode small strings and also now certain kinds of variants!

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
// and four more other, unused tags
```

Immediate patterns have two constraints:

1. The bit pattern must have the low bit set (to avoid conflicting with small
   integers)
1. The bit pattern must *not* have 001 as the low bits (to avoid conflicting
   with heap objects)

Everything else is fair game. We technically could use as many of the high bits
as we want and build in gigantic amounts of patterns but instead we are going
to limit ourselves to eight kinds of immediates and reserve the high bits for
use in those immediates[^escape-hatch]. For example, in *small strings*.

[^escape-hatch]: We could have an "escape hatch" where we have one low-bits
    encoding that says "look at the rest of the high bits to figure out what
    this is" but we're trying to avoid a two-step decoding situation.

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
    I wrote for a malware course. Instead of encoding stack strings by hand, I
    wrote a [crappy
    program](https://gist.github.com/tekknolagi/3df691ad6f48e85e26bd995f6ec5c900)
    to generate them for me for use in C programs.
    ```c
    uint64_t stack_strings[9] = {
    0x202c6f6c6c6548,
    0x2073692073696874,
    0x67617373656d2061,
    0x206d6f72662065,
    0x6972662072756f79,
    0x635320796c646e65,
    0x7069726373706172,
    0x6d6165742074,
    };
    stack_strings[8] = 1337;
    char* str_Hello__ = (char*)&stack_strings[0];
    char* str_this_is_a_message_from_ = (char*)&stack_strings[1];
    char* str_your_friendly_Scrapscript_team = (char*)&stack_strings[4];
    ```
    That gross blob is the result of encoding three strings:
    * `"Hello, ",`
    * `"this is a message from ",`
    * `"your friendly Scrapscript team",`


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
because we store it little-endian. This lets us shift byte-by-byte to read from
the start of the string to the end.

Here's the C code that the runtime uses to build non-constant strings:

```c
struct object* mksmallstring(const char* data, uword length) {
  assert(length <= kMaxSmallStringLength);
  uword result = 0;
  for (word i = length - 1; i >= 0; i--) {
    result = (result << kBitsPerByte) | data[i];
  }
  struct object* result_obj =
      (struct object*)((result << kBitsPerByte) |
                       (length << kImmediateTagBits) | kSmallStringTag);
  assert(!is_heap_object(result_obj));
  assert(is_small_string(result_obj));
  assert(small_string_length(result_obj) == length);
  return result_obj;
}

struct object* mkstring(struct gc_heap* heap, const char* data, uword length) {
  if (length <= kMaxSmallStringLength) {
    return mksmallstring(data, length);
  }
  struct object* result = mkstring_uninit_private(heap, length);
  memcpy(as_heap_string(result)->data, data, length);
  return result;
}
```

See also [Mike Ash's
post](https://mikeash.com/pyblog/friday-qa-2015-07-31-tagged-pointer-strings.html)
about small strings in Objective-C. They go way further with encoding tables
and stuff.

### Variants

As a refresher, variants are kind of like a dynamically typed version of
SML/OCaml variants. Whereas in OCaml and SML they are checked statically and
(mostly) erased at compile-time, Scrapscript keeps the tags around at run-time.
You can kind of think of it like adding one string's worth of metadata to an
object that you can match on.

```
eval =
| #int_lit x -> x
| #add [left, right] -> eval left + eval right
```

Until this past weekend, variants were all heap-allocated pairs of tag and
value.

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
`#some_tag [a, b]` allocates an object first for the list---several cons cells,
in fact---and then *also* for the tagged wrapper object.

But I vaguely recalled something about OCaml encoding variants with only a
couple of bits so I checked out their [page on data
representations](https://ocaml.org/docs/memory-representation#blocks-and-values).
On it, they have something that says:

> `Foo | Bar` variants are stored as ascending OCaml ints, starting from 0.

I was initially worried that OCaml could only do this because they have type
inference and therefore the compiler knows much more about what type every
AST/IR node is. Then I thought about it some more and realized that we still
have that information---just at run-time, with the pointer tagging. If we
combine our low-bits pointer tagging with the existing tag enum stuff, we're
set.

So I added a new kind of immediate to the compiler: immediate variants. They
don't support the full range of objects[^full-range], but they support any
tagged *hole*...

[^full-range]: I wonder if it would be possible to find a tagging scheme that
    supports immediate variants of *any* scrapscript object. Depending on the
    machine, we might run out of bits to encode the tag.

### True and false

...like `#true ()` and `#false ()`! These used to be heap-allocated, but now
they are immediate objects, and with no compiler special-casing. It falls
naturally out of the tag indexing and the pointer tagging.

This is pretty great, because it makes pattern matching on booleans---or any
similar tag---much faster. Look at the code that the compiler can now generate:

```
| #true () -> 123
```

turns into:

```
if (tmp_0 != mk_immediate_variant(Tag_true)) { goto case_1; }
    2301:	48 8b 54 24 08       	mov    rdx,QWORD PTR [rsp+0x8]
    2306:	b8 f6 00 00 00       	mov    eax,0xf6
    230b:	48 83 fa 0f          	cmp    rdx,0xf
    230f:	74 0b                	je     231c <fn_1+0x4c>
```

(where `tmp_0`/`[rsp+0x8]`/`rdx` is the match argument, `0xf6` is the small int
for `123`, and `0xf` is the tagged pointer for `# true ()`).

I want to emphasize that these small strings, small ints, and other immediates
are available *at run-time too*. They are not just limited to compiler
constants. This means we have smart constructors such as `mkstring` that
dynamically dispatch to either encoding a small string or heap-allocating a
large one.

This is great, but small strings and variants leave a lot still allocated.
Sometimes in scraps there are entire constant data structures that get
allocated at run-time, every time the parent closure is called. So we did
something about that.

## The const heap

Consider the list `[1, 2, 3]`. The compiler and its runtime system represent it
as three cons cells, `1 -> 2 -> 3 -> nil`. All of the cells contain data that
is known constant---integer literals---and point to other constant data. It
turns out that if we detect this at compile-time, we can allocate all of the
cons cells as constant C globals.

```python
{%- raw -%}
class Compiler:
    def compile(self, env: Env, exp: Object) -> str:
        if self._is_const(exp):
            return self._emit_const(exp)
        # ...

    def _emit_const(self, exp: Object) -> str:
        if isinstance(exp, Int):
            # TODO(max): Bignum
            return f"_mksmallint({exp.value})"
        if isinstance(exp, List):
            items = [self._emit_const(item) for item in exp.items]
            result = "empty_list()"
            for item in reversed(items):
                result = self._const_cons(item, result)
            return result
        # ...

    def _const_cons(self, first: str, rest: str) -> str:
        return self._const_obj("list", "TAG_LIST", f".first={first}, .rest={rest}")

    def _const_obj(self, type: str, tag: str, contents: str) -> str:
        result = self.gensym(f"const_{type}")
        self.const_heap.append(f"CONST_HEAP struct {type} {result} = {{.HEAD.tag={tag}, {contents} }};")
        return f"ptrto({result})"
{% endraw -%}
```

The `const_heap` is a list of structs that we emit at the top-level after
traversing the whole AST for normal compilation. It's meant to look similar to
our garbage-collected heap, but without actually using any of the GC's API.
Instead of calling `allocate`, we emit the structs directly. See the before,
where every time the function is called, it allocates:

```c
struct object* scrap_main() {
  HANDLES();
  OBJECT_HANDLE(tmp_0, empty_list());
  OBJECT_HANDLE(tmp_1, list_cons(_mksmallint(3), tmp_0));
  OBJECT_HANDLE(tmp_2, list_cons(_mksmallint(2), tmp_1));
  OBJECT_HANDLE(tmp_3, list_cons(_mksmallint(1), tmp_2));
  return tmp_3;
}
```

and after our const heap changes:

```c
#define CONST_HEAP const __attribute__((section("const_heap")))
CONST_HEAP struct list const_list_0 = {.HEAD.tag=TAG_LIST, .first=_mksmallint(3), .rest=empty_list() };
CONST_HEAP struct list const_list_1 = {.HEAD.tag=TAG_LIST, .first=_mksmallint(2), .rest=ptrto(const_list_0) };
CONST_HEAP struct list const_list_2 = {.HEAD.tag=TAG_LIST, .first=_mksmallint(1), .rest=ptrto(const_list_1) };
struct object* scrap_main() {
  HANDLES();
  return ptrto(const_list_2);
}
```

Hey presto, the main function just returns a pointer to constant data instead
of allocating anything at run-time.

In order of appearance, here are some of the confusing parts of the code:

`CONST_HEAP` is macro that directs the linker to put the attached object into
our custom section, `const_heap`. We also mark this data `const` so that the C
compiler can optimize references to it. I should probably also mark it `static`
and add some kind of `__attribute__((aligned(8)))` to make sure that we get the
same heap object tagging support.

`_mksmallint` is a macro that generates a small integer by shifting it left 1
bit.

`empty_list` is a macro that generates a tagged pointer to the empty list.

`ptrto` takes the address of constant data and then tags with `0b1` so that it
looks like a normal heap object. This means that there might be a bunch of
references to constant data flying around our garbage-collected heap. That's a
big problem because our GC attempts to *write to the data* and then *update
references to it*, so we have to be catch this:

```c
// NEW
extern char __start_const_heap[];
extern char __stop_const_heap[];

bool in_const_heap(struct gc_obj* obj) {
  return (uword)obj >= (uword)__start_const_heap &&
         (uword)obj < (uword)__stop_const_heap;
}
// END NEW

void visit_field(struct object** pointer, struct gc_heap* heap) {
  if (!is_heap_object(*pointer)) {
    return;
  }
  struct gc_obj* from = as_heap_object(*pointer);
  // NEW
  if (in_const_heap(from)) {
    return;
  }
  // END NEW
  struct gc_obj* to = is_forwarded(from) ? forwarded(from) : copy(heap, from);
  *pointer = heap_tag((uintptr_t)to);
}
```

We do this by using the linker-provided symbols `__start_const_heap` and
`__stop_const_heap` to get the bounds of the `const_heap` section. Then we can
see if a pointer is within those bounds before trying to forward it.

We support pretty much all constant versions of objects, including:

* Integers (currently only tagged small integers, but bignums would be similar)
* Strings
* Variants
* Lists
* Records
* Closures (!)

Yes, we also support constant closures. For now, this is only available for
closures that don't capture any variables. We could support closures that
capture only constant data, but that would require a little bit of finessing to
keep a record of which variables are constant and which aren't. I want to
figure out a way to do this in only 1 or 2 extra lines of code if possible.

```c
/*
. fact =
| 0 -> 1
| n -> n * fact (n - 1)
*/
#define ptrto(obj) ((struct object*)((uword)&(obj) + 1))
CONST_HEAP struct closure const_closure_5 = {.HEAD.tag=TAG_CLOSURE, .fn=fact_1, .size=0 };
struct object* scrap_main() {
  HANDLES();
  return ptrto(const_closure_5);
}
```

Neat.

## Have suggestions?

I'm always interested in using Scrapscript as a compiler and runtime
playground. Send ideas or full implementations my way please.

I'm currently looking at immediate `double`s based on the
[OpenSmalltalk](https://clementbera.wordpress.com/2018/11/09/64-bits-immediate-floats/).
We don't use them often in Scrapscript but it could be fun. I'm also looking at
how [ChakraCore](https://abchatra.github.io/TaggedFloat/) did it.

## Playing with the compiler

Try running `./scrapscript.py compile --compile examples/0_home/factorial.scrap` which
will produce both `output.c` and `a.out`. Then you can run `./a.out` to see the
result of your program.

We also now have a [compiler web REPL](https://scrapscript.fly.dev/compilerepl)
if you don't want to download anything.

## Thanks for reading

Want to learn more? Well first, play with [the web
REPL](https://scrapscript.fly.dev/repl). Then take a look at [the
repo](https://github.com/tekknolagi/scrapscript) and start contributing! Since
we don't have a huge backlog of well-scoped projects just yet, I recommend
posting in the [discourse group](https://scrapscript.discourse.group/) first to
get an idea of what would be most useful and also interesting to you.

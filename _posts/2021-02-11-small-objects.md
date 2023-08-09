---
title: Small objects and pointer tagging
layout: post
date: 2021-02-11 00:00:00 PT
description: Optimizing bytecode interpreters by avoiding heap allocation
series: runtime-opt
---

Welcome back to the third post in what is quickly turning into a series on
runtime optimization. The last two posts were about [inline
caching](/blog/inline-caching/) and
[quickening](/blog/inline-caching-quickening), two techniques for speeding up
the interpreter loop.

In this post, we will instead look at a different part of the runtime: the
object model.

## The problem

Right now, we represent objects as *tagged unions*.

```c
typedef enum {
  kInt,
  kStr,
} ObjectType;

typedef struct {
  ObjectType type;
  union {
    const char *str_value;
    word int_value;
  };
} Object;
```

This C struct contains two components: a tag for the type, and space for either
an integer or a C string pointer. In order to create a new `Object`, we
allocate space for it on the C heap and pass a pointer around. For example,
here is the current constructor for integers:

```c
Object* new_int(word value) {
  Object* result = malloc(sizeof *result);
  CHECK(result != NULL && "could not allocate object");
  *result = (Object){.type = kInt, .int_value = value};
  return result;
}
```

I don't know about you, but to me this seems a little wasteful. Allocating
**every single** new integer object on the heap leaves a lot to be desired.
`malloc` is slow and while memory is cheaper these days than it used to be, it
still is not free. We'll need to do something about this.

In addition, even if we somehow reduced allocation to the bare minimum, reading
from and writing to memory is still slow. If we can avoid that, we could
greatly speed up our runtime.

This post will focus on a particular strategy for optimizing operations on
integer objects, but the ideas also apply broadly to other small objects. See
[Exploring further](#exploring-further) for more food for thought.

## The solution space

There are a number of strategies to mitigate this gratuitous allocation, most
commonly:

1. Interning a small set of canonical integer objects. CPython does this for
   the integers between -5 and 256.
2. Interning all integers by looking them up in a specialized hash table before
   allocating. This is also called *hash consing*.
3. Have a statically typed low-level language where the compiler can know ahead
   of time what type every variable is and how much space it requires. C and
   Rust compilers, for example, can do this.

The first two approaches don't reduce the memory traffic, but the third
approach does. Our runtime has no such type guarantees and no compiler to speak
of, so that's a no-go, and I think we can do better than the first two
strategies. We'll just need to get a little clever and re-use some old
knowledge from the 80s.

## What's in a pointer?

Before we get clever, we should take a step back and think about the `Object`
pointers we pass around. The C standard
[guarantees](https://en.cppreference.com/w/c/memory/malloc) that `malloc` will
return an aligned pointer. On 32-bit systems, this means that the result will
be 4-byte aligned, and on 64-bit systems, it will be 8-byte aligned. This post
will only focus on 64-bit systems, so for our purposes all `malloc`ed objects
will be 8-byte aligned.

Being 8-byte aligned means that all pointers are **multiples of 8**. This means
that if you look at the pointer representation in binary, they look like:

```
High                                                           Low
0bxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx000
```

See that? The three lowest bits are zero. Since we're guaranteed the pointers
will always be given to us with the three zero bits, we can use those bits to
store some extra information. Lisp and Smalltalk runtimes have been doing this
for at least 30 years. OCaml does precisely this for its 63-bit integers, too.

On some hardware, there are also bits unused in the high part of the address.
We will only use the lower part of the address, though, because the high bits
are [reserved for future use](https://stackoverflow.com/questions/16198700/using-the-extra-16-bits-in-64-bit-pointers).

## The scheme

To start, we will tag all pointers to heap-allocated objects with a lower bit
of 1[^1]. This means that now all real heap pointers will end in `001` instead
of `000`.  We will then assume that any pointer with a lowest bit of 0 is
actually an integer. This leaves us 63 bits of integer space. This is one less
bit than we had before, which we will talk about more in
[Exploring further](#exploring-further).

We are doing this because the assumption behind this pointer tagging is that
integer objects are both small **and common**. Adding and subtracting them
should be very cheap. And it's not so bad if all operations on pointers have to
remove the low 1 bit, either. x86-64 addressing modes make it easy to fold that
into normal struct member reads and writes[^2].

And guess what? The best part is, since we were smart and used helper functions
to allocate, type check, read from, and write to the objects, we don't even
need to touch the interpreter core or library functions. We only need to touch
the functions that work directly on objects. Let's take a look.

## New object representation

Okay, we should also look at at the struct definition. I think we should first
make `Object` opaque. We don't want anyone trying to dereference a tagged
pointer!

```c
struct Object;
typedef struct Object Object;
```

Now we'll need to represent the rest of the heap-allocated objects in some
other type. I think `HeapObject` is a reasonable and descriptive name. We can
keep using the tagged union approach from earlier.

```c
typedef struct {
  ObjectType type;
  union {
    const char* str_value;
  };
} HeapObject;
```

Right now we only have strings but I imagine it would be useful to add some
more types later on.

[//]: # (TODO(max): Figure out where to put this paragraph)

Now, it's important to keep the invariant that **whenever we have a
`HeapObject*`, it is a valid pointer**. This means that we should always untag
before casting from `Object*` to `HeapObject*`. This will help both keep our
interfaces clean and avoid bugs. You'll see what I mean in a little bit.

## Helper functions

Now that we have our object representation down, we can take a look at the
helper functions. Let's start with the easiest three, `object_is_int`,
`object_as_int`, and `new_int`:

```c
enum {
  kIntegerTag = 0x0,      // 0b0
  kIntegerTagMask = 0x1,  // 0b1
  kIntegerShift = 1,
};

bool object_is_int(Object* obj) {
  return ((uword)obj & kIntegerTagMask) == kIntegerTag;
}

word object_as_int(Object* obj) {
  CHECK(object_is_int(obj));
  return (word)obj >> kIntegerShift;
}

Object* new_int(word value) {
  CHECK(value < INTEGER_MAX && "too big");
  CHECK(value > INTEGER_MIN && "too small");
  return (Object*)((uword)value << kIntegerShift);
}
```

We decided that integer tags are one bit wide, zero, and the lowest bit. This
function puts that in code. If you are unfamiliar with bit manipulation, check
out the Wikipedia article on [bit
masks](https://en.wikipedia.org/wiki/Mask_(computing)). The constants
`INTEGER_MAX` and `INTEGER_MIN` refer to the maximum and minimum values that
will fit in the 63 bits of space we have. Right now there are some `CHECK`s
that will abort the program if the integer does not fit in 63 bits.
Implementing a fallback to heap-allocated 64-bit integers or even big integers
is a potential extension (see [Exploring further](#exploring-further)).

The test for heap objects is similar to the test for ints. We use the same tag
width (one bit) but we expect the bit to be 1, not 0.

```c
enum {
  // ...
  kHeapObjectTag = 0x1,      // 0b01
  kHeapObjectTagMask = 0x1,  // 0b01
};

bool object_is_heap_object(Object* obj) {
  return ((uword)obj & kHeapObjectTagMask) == kHeapObjectTag;
}
```

Any pointer that passes `object_is_heap_object` should be dereferenceable once
unmasked.

Speaking of unmasking, we also have a function to do that. And we also have a
function that can cast the other way, too.

```c
HeapObject* object_address(Object* obj) {
  CHECK(object_is_heap_object(obj));
  return (HeapObject*)((uword)obj & ~kHeapObjectTagMask);
}

Object* object_from_address(HeapObject* obj) {
  return (Object*)((uword)obj | kHeapObjectTag);
}
```

The function `object_address` will be the only function that returns a
`HeapObject*`. It checks that the object passed in is actually a heap object
before casting and untagging. This should be safe enough.

Alright, so we can make integers and cast between `Object*` and `HeapObject*`.
We still need to think about `object_type` and the string functions.
Fortunately, they can mostly be implemented in terms of the functions we
implemented above!

Let's take a look at `object_type`. For non-heap objects, it has to do some
special casing. Otherwise we can just pull out the `type` field from the
`HeapObject*`.

```c
ObjectType object_type(Object* obj) {
  if (object_is_int(obj)) {
    return kInt;
  }
  return object_address(obj)->type;
}
```

And now for the string functions. These are similar enough to their previous
implementations, with some small adaptations for the new object model.

```c
bool object_is_str(Object* obj) { return object_type(obj) == kStr; }

const char* object_as_str(Object* obj) {
  CHECK(object_is_str(obj));
  return object_address(obj)->str_value;
}

Object* new_str(const char* value) {
  HeapObject* result = malloc(sizeof *result);
  CHECK(result != NULL && "could not allocate object");
  *result = (HeapObject){.type = kStr, .str_value = value};
  return object_from_address(result);
}
```

That's that for the helper functions. It's a good thing we implemented
`int_add`, `str_print`, etc in terms of our helpers. None of those have to
change a bit.

## Performance analysis

I am not going to run a tiny snippet of bytecode in a loop and call it faster
than the previous interpreter. See [Performance
analysis](/blog/inline-caching/#performance-analysis) from the first post for
an explanation.

I **am**, however, going to walk through some of the generated code for
functions we care about.

We said that integer operations were important, so let's take a look at what
kind of code the compiler generates for `int_add`. For a refresher, let's look
at the definition for `int_add` (which, mind you, has not changed since the
last post):

```c
Object* int_add(Object* left, Object* right) {
  CHECK(object_is_int(left));
  CHECK(object_is_int(right));
  return new_int(object_as_int(left) + object_as_int(right));
}
```

Previously this would read from memory in `object_as_int`, call `malloc` in
`new_int`, and then write to memory. That's a whole lot of overhead and
function calls. Even if `malloc` were free, memory traffic would still take
quite a bit of time.

Now let's take a look at the code generated by a C compiler. To get this code,
I pasted interpreter.c into [The Compiler Explorer](https://godbolt.org/). You
could also run `objdump -S ./interpreter` or
`gdb -batch -ex "disassemble/rs int_add" ./interpreter`. Or even run GDB and
poke at the code manually. Anwyay, here's the generated code:

```
int_add:                                # @int_add
        and     rdi, -2
        lea     rax, [rdi + rsi]
        and     rax, -2
        ret
```

How about that, eh? What was previously a monster of a function is now **four
whole instructions**[^3] and no memory operations. Put that in your pipeline and
smoke it.

This is the kind of benefit we can reap from having small objects inside
pointers.

Thanks for reading! Make sure to check out [the
repo](https://github.com/tekknolagi/icdemo) and poke at the code.

## Exploring further

In this post, we made an executive decision to shrink the available integer
range by one bit. We didn't add a fallback to heap-allocated 64-bit
numbers[^smi-double]. This is an interesting extension to consider if you
occasionally need some big numbers. Or maybe, if you need really big numbers,
you could also add a fallback to heap allocated bignums! If you don't care at
all, it's not unreasonable to decide to make your integer operations cut off at
63 bits.

[^smi-double]: Fedor Indutny has a [great post](https://darksi.de/6.smis-and-doubles/)
    about an optimization for JS where math has fast inline code paths for
    small integers but falls back to heap-allocated numbers instead. 

This post spent a decent chunk of time fitting integers into pointers. I chose
to write about integers because it was probably the simplest way to demonstrate
pointer tagging and immediate objects. However, your application may very well
not be integer heavy. It's entirely possible that in a typical workload, the
majority of objects floating around your runtime are small strings! Or maybe
floats, or maybe something else entirely. The point is, you need to measure and
figure out what **you** need before implementing something. Consider
implementing small strings or immediate booleans as a fun exercise. You will
have to think some more about your object tagging system!

Pointer tagging is not the only way to compress values into pointer-sized
objects. For runtimes whose primary numeric type is a double, it may make sense
to implement [NaN
boxing](https://bernsteinbear.com/pl-resources/#pointer-tagging-and-nan-boxing).
This is what VMs like SpiderMonkey[^4] and LuaJIT do.

Remember my suggestion about the template interpreter from the [quickening
post](/blog/inline-caching-quickening/)? Well, that idea is even more
attractive now. You, the runtime writer, get to write a lot less assembly. And
your computer gets to run a lot less code.

This post puts two distinct concepts together: small objects and pointer
tagging. Maybe you don't really need small objects, though, and want to store
other information in the tag bits of your pointer. What other kinds of
information could you store in there that is relevant to your workload? Perhaps
you can tag pointers to all prime integers. Or maybe you want to tag different
heap-allocated objects with their type tags. Either way, the two techniques are
independent and can be used separately.

## Notes

James Y Knight [posted
about](https://mail.python.org/pipermail/python-dev/2004-July/046139.html)
adding pointer tagging to CPython in 2004.

[^1]: In my [blog post](/blog/compiling-a-lisp-2/) about the [Ghuloum
      compiler](/assets/img/11-ghuloum.pdf), I used the bit patterns from the
      original Ghuloum paper to tag integers, characters, and different types
      of heap-allocated objects. Feel free to skim that if you want a taste for
      different pointer tagging schemes.

[^2]: (Adapted from my [Reddit comment](https://www.reddit.com/r/ProgrammingLanguages/comments/liix7g/unboxed_values_on_the_stack_and_automated_garbage/gnb9qxo/))

      Say you have a C struct:

      ```c
struct foo {
  int bar;
};
      ```

      and a heap-allocated instance of that struct:

      ```c
struct foo *instance = malloc(...);
      ```

      Reading an attribute of that struct in C looks like:

      ```c
instance->bar;
      ```

      and gets compiled down to something like the following pseudo-assembly
      (which assumes the `instance` pointer is stored in a register):

      ```
mov rax, [instance+offsetof(foo, bar)]
      ```

      This is read as:

      1. take pointer from whatever register `instance` is in
      2. add the offset for the `bar` field to the pointer
      3. dereference that resulting pointer
      4. store that in `rax`

      And if you tag your pointers with, say, `0x1`, you want to remove the
      `0x1`. Your C code will look like:

      ```c
instance->bar & ~0x1;
      ```

      or maybe:

      ```c
instance->bar - 1;
      ```

      Which compiles down to:

      ```
mov rax, [instance+offsetof(foo, bar)-1]
      ```

      and since both `offsetof(foo, bar)` and `1` are compile-time constants,
      that can get folded into the same `mov` instruction.

[^3]: And guess what? This is just what the C compiler can generate from a C
      description of our object model. I have not figured out how to add the
      right compiler hints, but another correct implementation of `int_add` is
      **just two instructions**:

      ```
int_add:                                # @int_add
        lea     rax, [rdi + rsi]
        ret
      ```

      I'm not sure what's going on with the code generation that prevents this
      optimization, but we really should be able to add two integers without
      doing any fiddling with tags. In an assembly/template interpreter world,
      this kind of fine-grained control becomes much easier.

[^4]: This is interesting because [V8](https://v8.dev/blog/pointer-compression)
      and [Dart](https://dart.dev/articles/archive/numeric-computation), other
      JS(-like) VMs use pointer tagging. Seach "Smi" (or perhaps "Smi integer")
      if you want to learn more.

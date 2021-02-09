---
title: Small objects and pointer tagging
layout: post
date: 2021-02-08 00:00:00 PT
description: Optimizing bytecode interpreters by avoiding heap allocation
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
here is the constructor for integers:

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

This post will focus on a particular strategy for integer objects, but the
ideas apply broadly to other small objects. See [Exploring
further](#exploring-further) for more food for thought.

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

Our runtime has no such type guarantees and no compiler to speak of, but I
think we can do better than the first two strategies. We'll just need to get a
little clever.

## What's in a pointer?

Before we get clever, we should take a step back and think about the `Object`
pointers we pass around.

### Alignment

* `char*` has 8 byte alignment on 64-bit systems [structure packing](http://www.catb.org/esr/structure-packing/#_padding)
* Enum has [unspecified alignment](http://www.catb.org/esr/structure-packing/#_awkward_scalar_cases),
  could be anywhere from 1 to 8 bytes
* x86 vs ARM? any difference?
* `_Alignas` [since C11](https://en.cppreference.com/w/c/language/_Alignas)
* Just assert `sizeof (HeapObject) >= 8` if the padding rules are too confusing
  and at worst add padding (struct layout vs manual layout, etc)
* size at least 8 means that last 3 bits of pointer are 0. so much room for
  activities

## Tradeoffs and big integers

We didn't have big integers before, but if we need the full 64 bits we can
overflow to a heap-allocated object if need be. Or if you don't care, just make
overflow undefined/wrap.

## Exploring further

* Small strings
* True/false

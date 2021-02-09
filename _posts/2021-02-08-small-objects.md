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
still is not free.

There are a number of strategies to mitigate this gratuitous allocation, most
commonly:

1. Interning a small set of canonical integer objects. CPython does this for
   the integers between -5 and 256.
2. Interning all integers by looking them up in a specialized hash table before
   allocating. This is also called *hash consing*.

In this post we will focus instead on a third, different strategy: avoid heap
allocating numbers that require fewer than 63 bits.

Statically typed low-level languages like C or Rust do this by knowing ahead of
time what type every variable is and how much space it requires. Our runtime
has no such type guarantees, so we'll need to get a little clever.

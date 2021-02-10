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
pointers we pass around. C
[guarantees](https://en.cppreference.com/w/c/memory/malloc) that `malloc` will
return an aligned pointer. On 32-bit systems, this means that the result will
be 4-byte aligned, and on 64-bit systems, it will be 8-byte aligned. This post
will only focus on 64-bit systems, so for our purposes all `malloc`ed objects
will be 8-byte aligned.

Being 8-byte aligned means that all pointers are **multiples of 8**. And if you
look at the pointer representation in binary, they look like:

```
High                                                           Low
0bxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx000
```

See that? The three lowest bits are zero. Since we're guaranteed the poitners
will always be given to us with the three zero bits, we can use those bits to
store some extra information.

On some hardware, there are also bits unused in the high part of the address.
We will only use the lower part of the address, though, because the high bits
are reserved for future use.

## The scheme

To start, we will tag all pointers to heap-allocated objects with a lower bit
of 1[^1]. This means that now all pointers will end in `001` instead of `000`.
We will then assume that any pointer with a lowest bit of 0 is actually an
integer. This leaves us 63 bits of integer space. This is one less bit than we
had before, which we will talk about more in [Tradeoffs](#tradeoffs).

We are doing this because the assumption behind this pointer tagging is that
integer objects are both small **and common**. Adding and subtracting them
should be very cheap. And it's not so bad if all operations on pointers have to
remove the low 1 bit, either. x86-64 addressing modes make it easy to fold that
into normal struct member reads and writes.

And guess what? The best part is, since we were smart and used helper functions
to allocate, type check, read from, and write to the objects, we don't even
need to touch the interpreter core or library functions. We only need to touch
the functions that work directly on objects. Let's take a look.

## New object representation

```c
struct Object;
typedef struct Object Object;

typedef struct {
  ObjectType type;
  union {
    const char* str_value;
  };
} HeapObject;
```



```c
bool object_is_int(Object* obj) {
  return ((uword)obj & kIntegerTagMask) == kIntegerTag;
}

bool object_is_heap_object(Object* obj) {
  return ((uword)obj & kHeapObjectTagMask) == kHeapObjectTag;
}

HeapObject* object_address(Object* obj) {
  CHECK(object_is_heap_object(obj));
  return (HeapObject*)((uword)obj & ~kHeapObjectTagMask);
}

Object* object_new_heap_object(HeapObject* obj) {
  return (Object*)((uword)obj | kHeapObjectTagMask);
}

ObjectType object_type(Object* obj) {
  if (object_is_int(obj)) {
    return kInt;
  }
  return object_address(obj)->type;
}

bool object_is_str(Object* obj) { return object_type(obj) == kStr; }

word object_as_int(Object* obj) {
  CHECK(object_is_int(obj));
  return (uword)obj >> kIntegerShift;
}

const char* object_as_str(Object* obj) {
  CHECK(object_is_str(obj));
  return object_address(obj)->str_value;
}

Object* new_int(word value) {
  CHECK(value < INTEGER_MAX && "too big");
  CHECK(value > INTEGER_MIN && "too small");
  return (Object*)(value << kIntegerShift);
}

Object* new_str(const char* value) {
  HeapObject* result = malloc(sizeof *result);
  CHECK(result != NULL && "could not allocate object");
  *result = (HeapObject){.type = kStr, .str_value = value};
  return object_new_heap_object(result);
}
```

## Tradeoffs

We didn't have big integers before, but if we need the full 64 bits we can
overflow to a heap-allocated object if need be. Or if you don't care, just make
overflow undefined/wrap.


## Exploring further

* Small strings
* True/false
* NaN tagging for runtimes where all numbers are doubles

<hr style="width: 100px;" />
<!-- Footnotes -->

[^1]: In my [blog post](/blog/compiling-a-lisp-2/) about the [Ghuloum
      compiler](/assets/img/11-ghuloum.pdf), I used the bit patterns from the
      original Ghuloum paper to tag integers, characters, and different types
      of heap-allocated objects. Feel free to skim that if you want a taste for
      different pointer tagging schemes.

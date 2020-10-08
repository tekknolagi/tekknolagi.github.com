---
title: "Compiling a Lisp: Heap allocation"
layout: post
date: 2020-10-07 23:30:36 PDT
---

<span data-nosnippet>
*[first]({% link _posts/2020-08-29-compiling-a-lisp-0.md %})* -- *[previous]({% link _posts/2020-10-07-compiling-a-lisp-8.md %})*
</span>

Welcome back to the "Compiling a Lisp" series. Last time we added support for
`if` expressions. This time we're going add support for heap allocation.

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
The first element is the `car` and the second one is the `cdr`.

```
   +-----+-----+
...| car | cdr |...
   +-----+-----+
   ^
   pointer
```

The untagged pointer points to the address of the first element, and the tagged
pointer has some extra information that we need to get rid of to inspect the
elements.

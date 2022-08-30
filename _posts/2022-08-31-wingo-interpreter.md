---
title: "Interpreter techniques"
layout: post
date: 2022-08-31
---

A bit ago, Andy Wingo put out a [very interesting blog post][blog post] about
WASM code generation *from within WASM*---a just-in-time compiler. He explained
how it worked, the challenges, and even had a browser-based demo. Very cool.

[blog post]: https://wingolog.org/archives/2022/08/18/just-in-time-code-generation-within-webassembly

As a bonus, if you dive into his compact code, there's a lot more interesting
stuff that he did not touch on in the blog post. The first 727 lines of
[interp.cc][interp.cc] contain an AST interpreter that showcases small and
subtle interpreter techniques:

[interp.cc]: https://github.com/wingo/wasm-jit/blob/2477dfcbde9ec6e09f62f0fd42a4f73ac11bad41/interp.cc

* name to index conversion
* semispace (Cheney) garbage collection
* precise rooting in native (C++) code using handles
* pointer tagging
* small objects in tagged pointers
* tail calls

TODO: transition

## Name conversion

Many hobby interpreters implement variables by keeping names in the AST or in
the bytecode and then looking up the names in hash tables. Even early versions
of CPython, the dominant Python implementation, did this. It works, but it
means that every variable name use at runtime incurs some costs: hashing and
pointer chasing.

notes:

* lambdas and function calls take precisely one argument
* primitives get precisely two arguments and are evaluated differently
* `Env::lookup` is linear in the number of variables live because each `Env`
  only has one captured value
* the function argument is passed in an `Env`

## Cheney GC

see `Heap::collect`

notes:

* `Heap` manages rootset and traverses these roots (`Heap::visitRoots`) on
  `Heap::collect`
  * TODO: explain the loop after `visitRoots`
* every object has a `visitFields` method that allows the heap to move an
  object
  * `Heap::copy`
    * copies the entire object into newspace
    * "forwards" the object by writing over it in oldspace
    * adjusts the heap pointer by the number of ... TODO
  * `Heap::visit` replaces the oldspace pointer with a newspace pointer
  * why does `Heap::visit` need to check for null?
* in the fast case, `Heap::allocate` bumps a pointer. in the slow case, full
  collection
* `Heap::copy` and `Heap::scan` have a special case for each type of
  heap-allocated object
  * it's also possible to do this with a header that encodes how many fields
    are in an object
  * it's easiest to do this if every field in a heap-allocated object is also
    an object pointer (`Value`)

## Handles

## Pointer tagging

## Small objects

## Tail calls

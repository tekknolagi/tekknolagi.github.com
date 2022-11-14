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

Many small interpreters implement their own garbage collection mechanism. I
link to a couple on my [PL resources page](/pl-resources/). A lot of the C and
C++ interpreters use this simple but difficult to get right dance: pushing and
popping roots.

```c
object* do_another_thing(object* a, object* b, object* c, object* d);

object* do_something(object* a, object* b) {
  object* c = gc_alloc();
  GC_PROTECT(c);
  object* d = gc_alloc();
  GC_PROTECT(d);
  object* e = do_another_thing(a, b, c, d);
  GC_UNPROTECT(c);
  GC_UNPROTECT(d);
  return e;
}
```

This is hard to get right because:

1. You have to remember to protect all the roots you need so they don't get
   freed out from under you
2. You have to remember to *unprotect* all the roots or your GC might try to
   poke at invalid stack memory
3. The protect/unprotect does not align with variable scope, which leaves you
   at risk of accidentally writing a use-after-unprotect, especially during
   refactoring
4. You have to remember to protect/unprotect in the same order, since the
   common way to do this is to push to/pop from a stack

Fortunately, there is a better solution: handles. Handles lean on C++ RAII to
match GC protection with variable scope. If you ensure that GC-allocated
pointers *only* live in handles within a function, you're golden. Consider a
handle version of the above code:

```c++
object* do_another_thing(const Rooted<object>& a, const Rooted<object>& b,
                         const Rooted<object>& c, const Rooted<object>& d);

object* do_something(const Rooted<object>& a, const Rooted<object>& b) {
  Rooted<object> c(gc_alloc());
  Rooted<object> d(gc_alloc());
  Rooted<object> e(do_another_thing(a, b, c, d));
  return e.get();
}
```

Notice that functions take const references to handles as parameters. They take
handles so that the underlying pointers are safe during GC. They take const
references to handles because of the aforementioned RAII rules:

1. On construction, they register the underlying pointer with the GC
2. On destruction, they deregister the pointer

Which means that passing around handles by copying them is probably correct but
slow.

Also notice that despite taking handles as parameters, functions return raw
`object` pointers. Since nothing can happen after return (and the new owner of
the value should be putting this value in a handle anyway), the function
returns a raw pointer. Now you "only" have to deal with normal C/C++
use-after-free.

Last, notice that there is no explicit ordering of unprotect; C++ guarantees
destructor order is the reverse of constructor order. Last allocated is first
destroyed.

This handle technique works for both moving and non-moving GCs.

Handles aren't all rainbows and sunshine: they do come with a performance
penalty. To keep you safe, the `Rooted` class holds an `object` pointer as a
member and (for moving GCs) updates its value when a GC runs. This helps keep
the raw pointer off of the native stack. This also means that there is some
indirection for all handle operations, which is not ideal.

Handles are not my invention; they are used [in
Spidermonkey][spidermonkey-handles], [in V8][v8-handles] (see also [this
one][v8-handles-2]), in the [Hotspot JVM][hotspot-handles], in
[Dart][dart-handles], in the [Skybison][skybison-handles] Python runtime, and
probably many more.

[spidermonkey-handles]: https://github.com/mozilla-spidermonkey/spidermonkey-embedding-examples/blob/esr78/docs/GC%20Rooting%20Guide.md
[v8-handles]: https://v8.dev/docs/embed
[v8-handles-2]: https://blog.reverberate.org/2016/10/17/native-extensions-memory-management-part2-javascript-v8.html
[hotspot-handles]: https://github.com/openjdk/jdk/blob/master/src/hotspot/share/runtime/handles.hpp
[dart-handles]: https://github.com/dart-lang/sdk/blob/main/runtime/vm/handles.h
[skybison-handles]: https://github.com/tekknolagi/skybison

It's probably possible to optimize handles a bit if you can give your compiler
knowledge of your native function's stack layout. Being able to integrate with
`llvm.gcroot` would be very neat. Kind of a fusion of the usual [shadow
stack][shadow-stack] approach for generated code and manual handles for runtime
code. I have not yet found a project that does this. Perhaps the APIs are not
stable enough.

[shadow-stack]: https://dl.acm.org/doi/10.1145/512429.512449

Notes to come back to later on shadow stacks:

* Terence Parr's [comments in llvm-dev](https://groups.google.com/g/llvm-dev/c/M4HOyteR4J4)
  about this problem, which is fun to see because he gave [the
  talk](https://www.youtube.com/watch?v=OjaAToVkoTw) that helped me understand
  bytecode interpreters for the very first time
* The [LLVM docs](https://releases.llvm.org/3.5.2/docs/GarbageCollection.html)
  about this that assume you are generating LLVM from your compiler, not
  writing a runtime in C++
* Maybe the new [Statepoint API](https://llvm.org/docs/Statepoints.html) is
  worth looking into
  * [llvm-statepoint-utils](https://github.com/kavon/llvm-statepoint-utils)
    repo

## Pointer tagging

## Small objects

## Tail calls

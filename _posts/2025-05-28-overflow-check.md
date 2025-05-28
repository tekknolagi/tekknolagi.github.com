---
title: "Zero-overhead checks with fake stack overflows"
layout: post
---

Imagine you have a virtual machine for a dynamic language. It supports starting
multiple threads from within the guest language via some API. This is
immediately challenging right out the gate because it requires keeping some key
runtime-internal state in sync:

* memory allocator / garbage collector
* compiler
* other bookkeeping

Instead of locking these components so that only one thread can use those
shared resources at a time, runtime implementors tend to instead shard them:
each thread gets its own slice to work with privately.

This means that each thread might have a separate mini-heap that it can use
privately until exhaustion, and only then request a stop-the-world event. But
how do we indicate that all threads should stop?

We could do something like check for a flag every N instructions, but this
introduces significant overhead in the interpreter and does not play well with
a compiler. Instead, ideally, there would be no overhead in the fast and normal
case.

VM implementors have decided to solve this by folding the synchronization check
into an existing check. [Skybison][], for example, folds this check into the
stack overflow check.

On frame push, we have to check for a stack overflow anyway, so we can re-use
the slow path. If the main thread wants to synchronize the other
threads---because the programmer hit Control-C in the REPL, for example,
causing an interrupt---it can artificially shrink the maximum stack size of the
other threads. Then, the next time the other threads call a function, they will
hit this overflow check and go to the slow path. In the slow path, they can
disambiguate by checking another flag somewhere else.

[Skybison]: https://github.com/tekknolagi/skybison

```c++
Frame* Thread::pushNativeFrame(word nargs) {
  word locals_offset = Frame::kSize + nargs * kPointerSize;
  if (UNLIKELY(wouldStackOverflow(Frame::kSize))) {
    return handleInterruptPushNativeFrame(locals_offset);
  }
  return pushNativeFrameImpl(locals_offset);
}

NEVER_INLINE Frame* Thread::handleInterruptPushNativeFrame(word locals_offset) {
  if (handleInterrupt(Frame::kSize)) {
    return nullptr;
  }
  Frame* result = pushNativeFrameImpl(locals_offset);
  handleInterruptWithFrame();
  return result;
}
```

V8 does [something very similar in Maglev][maglev] on function entry.

[maglev]: https://github.com/v8/v8/blob/3840a5c40c5ea1f44a8d9d534147e1d864e0bcf7/src/maglev/maglev-ir.cc#L1125

But not all applications involve regular function calls. Sometimes it's
possible to have a hot loop in which no frames are pushed to the call stack. In
this case, it might make sense to check for interrupts (or do other thread
synchronization checks) in loop back-edges. CPython [does this][cpython]. PyPy
does too.

```c
PyObject *eval(...) {
    // ...
    while (1) {
        // ...
        switch (opcode) {
        // ...
            case TARGET(JUMP_ABSOLUTE): {
                PREDICTED(JUMP_ABSOLUTE);
                JUMPTO(oparg);
                CHECK_EVAL_BREAKER();
                DISPATCH();
            }
        // ...
        }
    }
    // ...
}
```

[cpython]: https://github.com/python/cpython/blob/6322edd260e8cad4b09636e05ddfb794a96a0451/Python/ceval.c#L3846

I'm writing this post because PyPy [published a post][pypy] about a new feature
that uses a similar technique! For sampling profiling, they put their check
(whether or not to sample) behind a check for exhaustion of the nursery (small
heap).

[pypy]: https://pypy.org/posts/2025/02/pypy-gc-sampling.html

Maybe next time I will write about guard pages, which I recently added to
RBS...

---
title: "Inline caching: quickening"
layout: post
date: 2021-01-28 00:00:00 PT
description: Further optimizing bytecode interpreters by removing branches and indirection
---

In my [last post](/blog/inline-caching/) I discussed inline caching as a
technique for runtime optimization. I ended the post with some extensions to
the basic technique, like *quickening*.

Quickening involves bytecode rewriting --- self modifying code --- to remove
some branches and indirection in the common path.

## The problem

Let's take a look at a fragment of the caching interpreter from the last post
so we can talk about the problem more concretely.

```c
void add_update_cache(Frame *frame, Object left, Object right) {
  Method method = lookup_method(left.type, kAdd);
  cache_at_put(frame, left.type, method);
  Object result = (*method)(left, right);
  push(frame, result);
}

void eval_code_cached(Code *code, Object *args, int nargs) {
  // ...
  while (true) {
    // ...
    switch (op) {
      // ...
      case ADD: {
        Object right = pop(&frame);
        Object left = pop(&frame);
        CachedValue cached = cache_at(&frame);
        Method method = cached.value;
        if (method == NULL || cached.key != left.type) {
          add_update_cache(&frame, left, right);
          break;
        }
        Object result = (*method)(left, right);
        push(&frame, result);
        break;
      }
      // ...
    }
    frame.pc += kBytecodeSize;
  }
}
```

As I also mentioned last post, the `ADD` opcode handler has three cases to
handle:

1. Cache is empty
2. Cache has the wrong key
3. Cache has right key

Since Deutsch &amp; Schiffman found that types don't vary that much, the third
case is the fast path case. This means that we should do as little as possible
in that case. And right now, we're doing too much work.

Why should we have to check if the cache slot is empty if in the fast path it
shouldn't be? And why should we then have to make an indirect call? On some
CPUs, indirect calls are [much slower](calls) than direct calls. And this
assumes the compiler generates a call instruction --- it's very possible that a
compiler would decide to inline the direct call.

[calls]: https://stackoverflow.com/questions/7241922/how-has-cpu-architecture-evolution-affected-virtual-function-call-performance

Quickening is a technique that reduces the number of checks by explitly marking
state transitions in the bytecode.

## Removing the empty check

In order to remove one of the checks --- the `method == NULL` check --- we can
add a new opcode, `ADD_CACHED`. The `ADD_CACHED` opcode can skip the check
because our interpreter will maintain the following invariant:

**Invariant:** The opcode `ADD_CACHED` will appear in the bytecode stream *if
and only if* there is an entry in the cache at that opcode.

After `ADD` adds something to the cache, it can rewrite itself to `ADD_CACHED`.
This way, the next time around, we have satisfied the invariant.

Let's see how that looks:

```c
void add_update_cache(Frame *frame, Object left, Object right) {
  Method method = lookup_method(left.type, kAdd);
  cache_at_put(frame, left.type, method);
  Object result = (*method)(left, right);
  push(frame, result);
}

void eval_code_quickening(Code *code, Object *args, int nargs) {
  // ...
  while (true) {
    // ...
    switch (op) {
      // ...
      case ADD: {
        Object right = pop(&frame);
        Object left = pop(&frame);
        add_update_cache(&frame, left, right);
        code->bytecode[frame.pc] = ADD_CACHED;
        break;
      }
      case ADD_CACHED: {
        Object right = pop(&frame);
        Object left = pop(&frame);
        CachedValue cached = cache_at(&frame);
        if (cached.key != left.type) {
          add_update_cache(&frame, left, right);
          break;
        }
        Method method = cached.value;
        Object result = (*method)(left, right);
        push(&frame, result);
      }
    // ...
    }
    frame.pc += kBytecodeSize;
  }
}
```

Not too different. We've shuffled the code around a little bit but overall it
looks fairly similar. We still get to share some code in `add_update_cache`, so
there isn't too much duplication, either.

Now that we've moved the empty check, it's time to remove the indirect call.

## Removing the indirect call

Let's assume for a minute that you, the writer of a language runtime, know that
most of the time, when people write `a + b`, the operation refers to integer
addition.

Not many other primitive types implement addition. Frequently floating point
numbers use the same operator (though languages like OCaml do not). Maybe
strings. And maybe your language allows for overloading the plus operator. But
most people don't do that. They add numbers.

In that case, you want to remove as much of the overhead as possible for adding
two numbers. So let's introduce a new opcode, `ADD_INT` that is specialized for
integer addition.

In an ideal world, we would just be able to pop two objects, add them, and move
on. But in our current reality, we still have to deal with the possibility of
programmers passing in a non-integer every once in a while.

So first, we check if the types match. If they don't, we populate the cache and
transition to `ADD_CACHED`. I'll get to why we do that in a moment.

And if we did actually get an int, great, we call this new function
`do_add_int`.

```c
void do_add_int(Frame *frame, Object left, Object right) {
  Object result = int_add(left, right);
  push(frame, result);
}

void eval_code_quickening(Code *code, Object *args, int nargs) {
  // ...
  while (true) {
    // ...
    switch (op) {
      // ...
      case ADD_INT: {
        Object right = pop(&frame);
        Object left = pop(&frame);
        if (left.type != kInt) {
          add_update_cache(&frame, left, right);
          code->bytecode[frame.pc] = ADD_CACHED;
          break;
        }
        do_add_int(&frame, left, right);
        break;
      }
    // ...
    }
    frame.pc += kBytecodeSize;
  }
}
```

This is a nice opcode handler for `ADD_INT`, but right now it's orphaned. Some
opcode has to take the leap and rewrite itself to `ADD_INT`, otherwise it'll
never get run.

I suggest we make `ADD` do the transition. This keeps `ADD_CACHED` fast for
other types. If `ADD` observes that the left hand side of the operation is an
integer, it'll call `do_add_int` and rewrite itself.

```c
void eval_code_quickening(Code *code, Object *args, int nargs) {
  // ...
  while (true) {
    // ...
    switch (op) {
      // ...
      case ADD: {
        Object right = pop(&frame);
        Object left = pop(&frame);
        if (left.type == kInt) {
          do_add_int(&frame, left, right);
          code->bytecode[frame.pc] = ADD_INT;
          break;
        }
        add_update_cache(&frame, left, right);
        code->bytecode[frame.pc] = ADD_CACHED;
        break;
      }
    // ...
    }
    frame.pc += kBytecodeSize;
  }
}
```

Back to "why transition from `ADD_INT` to `ADD_CACHED`". Two thoughts:

1. We could transition back to `ADD`. In that case, this code would perform
   poorly in an environment where the programmer passes multiple different
   types at this opcode. There would be a lot of bytecode rewriting overhead
   going on as it goes back and forth between `ADD` and `ADD_INT`.

2. We could also assume it's a hiccup and *not* rewrite. This would perform
   poorly if the first time the argument is an integer, but something else
   every subsequent operation. There would be a lot of `lookup_method` calls.

A great extension here would be to add a polymorphic cache.

## Why is this faster?

## On code duplication

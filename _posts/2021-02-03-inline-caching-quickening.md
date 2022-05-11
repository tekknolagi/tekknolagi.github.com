---
title: "Inline caching: quickening"
layout: post
date: 2021-02-03 00:00:00 PT
description: Further optimizing bytecode interpreters by removing branches and indirection
series: runtime-opt
---

In my [last post](/blog/inline-caching/) I discussed inline caching as a
technique for runtime optimization. I ended the post with some extensions to
the basic technique, like *quickening*. If you have not read the previous post,
I recommend it. This post will make many references to it.

Quickening involves bytecode rewriting --- self modifying code --- to remove
some branches and indirection in the common path. Stefan Brunthaler writes
about it in his papers [Efficient Interpretation using Quickening][quickening]
and [Inline Caching Meets Quickening][ic-quickening].

[quickening]: https://publications.sba-research.org/publications/dls10.pdf
[ic-quickening]: https://publications.sba-research.org/publications/ecoop10.pdf

## The problem

Let's take a look at a fragment of the caching interpreter from the last post
so we can talk about the problem more concretely. You can also get the sources
[from the repo](https://github.com/tekknolagi/icdemo) and open `interpreter.c`
in your preferred editor.

```c
void add_update_cache(Frame* frame, Object* left, Object* right) {
  Method method = lookup_method(object_type(left), kAdd);
  cache_at_put(frame, object_type(left), method);
  Object* result = (*method)(left, right);
  push(frame, result);
}

void eval_code_cached(Frame* frame) {
  // ...
  while (true) {
    // ...
    switch (op) {
      // ...
      case ADD: {
        Object* right = pop(frame);
        Object* left = pop(frame);
        CachedValue cached = cache_at(frame);
        Method method = cached.value;
        if (method == NULL || cached.key != object_type(left)) {
          add_update_cache(frame, left, right);
          break;
        }
        Object* result = (*method)(left, right);
        push(frame, result);
        break;
      }
      // ...
    }
    frame->pc += kBytecodeSize;
  }
}
```

As I also mentioned last post, the `ADD` opcode handler has three cases to
handle:

1. Cache is empty
2. Cache has the wrong key
3. Cache has the right key

Since Deutsch &amp; Schiffman found that types don't vary that much, the third
case is the fast path case. This means that we should do as little as possible
in that case. And right now, we're doing too much work.

Why should we have to check if the cache slot is empty if in the fast path it
shouldn't be? And why should we then have to make an indirect call? On some
CPUs, indirect calls are [much slower][calls] than direct calls. And this
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

<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="186pt" height="133pt" viewBox="0.00 0.00 185.95 132.80">
<g id="graph0" class="graph" transform="scale(1 1) rotate(0) translate(4 128.8)">
<title>G</title>
<polygon fill="#ffffff" stroke="transparent" points="-4,4 -4,-128.8 181.9491,-128.8 181.9491,4 -4,4"/>
<!-- ADD -->
<g id="node1" class="node">
<title>ADD</title>
<ellipse fill="none" stroke="#000000" cx="76.4729" cy="-106.8" rx="31.9012" ry="18"/>
<text text-anchor="middle" x="76.4729" y="-102.6" font-family="Times,serif" font-size="14.00" fill="#000000">ADD</text>
</g>
<!-- ADD_CACHED -->
<g id="node2" class="node">
<title>ADD_CACHED</title>
<ellipse fill="none" stroke="#000000" cx="76.4729" cy="-18" rx="76.4459" ry="18"/>
<text text-anchor="middle" x="76.4729" y="-13.8" font-family="Times,serif" font-size="14.00" fill="#000000">ADD_CACHED</text>
</g>
<!-- ADD&#45;&gt;ADD_CACHED -->
<g id="edge1" class="edge">
<title>ADD-&gt;ADD_CACHED</title>
<path fill="none" stroke="#000000" d="M76.4729,-88.4006C76.4729,-76.2949 76.4729,-60.2076 76.4729,-46.4674"/>
<polygon fill="#000000" stroke="#000000" points="79.973,-46.072 76.4729,-36.072 72.973,-46.0721 79.973,-46.072"/>
<text text-anchor="middle" x="127.211" y="-58.2" font-family="Times,serif" font-size="14.00" fill="#000000">  non-int observed</text>
</g>
<!-- ADD_CACHED&#45;&gt;ADD_CACHED -->
<g id="edge2" class="edge">
<title>ADD_CACHED-&gt;ADD_CACHED</title>
<path fill="none" stroke="#000000" d="M145.2582,-26.0063C160.1477,-25.2234 170.9459,-22.5547 170.9459,-18 170.9459,-14.5484 164.7447,-12.1799 155.2612,-10.8943"/>
<polygon fill="#000000" stroke="#000000" points="155.5318,-7.4046 145.2582,-9.9937 154.904,-14.3764 155.5318,-7.4046"/>
</g>
</g>
</svg>

Let's see how that looks:

```c
void eval_code_quickening(Frame* frame) {
  // ...
  while (true) {
    // ...
    switch (op) {
      // ...
      case ADD: {
        Object* right = pop(frame);
        Object* left = pop(frame);
        add_update_cache(frame, left, right);
        code->bytecode[frame->pc] = ADD_CACHED;
        break;
      }
      case ADD_CACHED: {
        Object* right = pop(frame);
        Object* left = pop(frame);
        CachedValue cached = cache_at(frame);
        if (cached.key != object_type(left)) {
          add_update_cache(frame, left, right);
          break;
        }
        Method method = cached.value;
        Object* result = (*method)(left, right);
        push(frame, result);
        break;
      }
    // ...
    }
    frame->pc += kBytecodeSize;
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
void do_add_int(Frame* frame, Object* left, Object* right) {
  Object* result = int_add(left, right);
  push(frame, result);
}

void eval_code_quickening(Frame* frame) {
  // ...
  while (true) {
    // ...
    switch (op) {
      // ...
      case ADD_INT: {
        Object* right = pop(frame);
        Object* left = pop(frame);
        if (object_type(left) != kInt) {
          add_update_cache(frame, left, right);
          code->bytecode[frame->pc] = ADD_CACHED;
          break;
        }
        do_add_int(frame, left, right);
        break;
      }
    // ...
    }
    frame->pc += kBytecodeSize;
  }
}
```

This is a nice opcode handler for `ADD_INT`, but right now it's orphaned. Some
opcode has to take the leap and rewrite itself to `ADD_INT`, otherwise it'll
never get run.

I suggest we make `ADD` do the transition. This keeps `ADD_CACHED` fast for
other types. If `ADD` observes that the left hand side of the operation is an
integer, it'll call `do_add_int` and rewrite itself.

<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="338pt" height="222pt" viewBox="0.00 0.00 338.34 221.60">
<g id="graph0" class="graph" transform="scale(1 1) rotate(0) translate(4 217.6)">
<title>G</title>
<polygon fill="#ffffff" stroke="transparent" points="-4,4 -4,-217.6 334.3408,-217.6 334.3408,4 -4,4"/>
<!-- ADD -->
<g id="node1" class="node">
<title>ADD</title>
<ellipse fill="none" stroke="#000000" cx="145.8646" cy="-195.6" rx="31.9012" ry="18"/>
<text text-anchor="middle" x="145.8646" y="-191.4" font-family="Times,serif" font-size="14.00" fill="#000000">ADD</text>
</g>
<!-- ADD_INT -->
<g id="node2" class="node">
<title>ADD_INT</title>
<ellipse fill="none" stroke="#000000" cx="52.8646" cy="-106.8" rx="52.7295" ry="18"/>
<text text-anchor="middle" x="52.8646" y="-102.6" font-family="Times,serif" font-size="14.00" fill="#000000">ADD_INT</text>
</g>
<!-- ADD&#45;&gt;ADD_INT -->
<g id="edge1" class="edge">
<title>ADD-&gt;ADD_INT</title>
<path fill="none" stroke="#000000" d="M129.2381,-179.7243C115.1397,-166.2627 94.6879,-146.7345 78.4957,-131.2736"/>
<polygon fill="#000000" stroke="#000000" points="80.5851,-128.4293 70.9355,-124.0548 75.751,-133.4921 80.5851,-128.4293"/>
<text text-anchor="middle" x="147.5224" y="-147" font-family="Times,serif" font-size="14.00" fill="#000000">int observed</text>
</g>
<!-- ADD_CACHED -->
<g id="node3" class="node">
<title>ADD_CACHED</title>
<ellipse fill="none" stroke="#000000" cx="145.8646" cy="-18" rx="76.4459" ry="18"/>
<text text-anchor="middle" x="145.8646" y="-13.8" font-family="Times,serif" font-size="14.00" fill="#000000">ADD_CACHED</text>
</g>
<!-- ADD&#45;&gt;ADD_CACHED -->
<g id="edge2" class="edge">
<title>ADD-&gt;ADD_CACHED</title>
<path fill="none" stroke="#000000" d="M166.4552,-181.5644C174.6221,-175.4041 183.7438,-167.727 190.8646,-159.6 222.7179,-123.2457 242.1602,-95.1874 216.8646,-54 212.9363,-47.6038 207.4791,-42.3012 201.3185,-37.9161"/>
<polygon fill="#000000" stroke="#000000" points="202.9031,-34.7806 192.5592,-32.4885 199.2161,-40.7309 202.9031,-34.7806"/>
<text text-anchor="middle" x="279.6027" y="-102.6" font-family="Times,serif" font-size="14.00" fill="#000000">  non-int observed</text>
</g>
<!-- ADD_INT&#45;&gt;ADD_INT -->
<g id="edge4" class="edge">
<title>ADD_INT-&gt;ADD_INT</title>
<path fill="none" stroke="#000000" d="M100.2223,-115.0092C113.5209,-114.6497 123.7292,-111.9133 123.7292,-106.8 123.7292,-103.0849 118.3403,-100.6245 110.2926,-99.4189"/>
<polygon fill="#000000" stroke="#000000" points="110.4755,-95.9222 100.2223,-98.5908 109.9018,-102.8986 110.4755,-95.9222"/>
<text text-anchor="middle" x="163.387" y="-102.6" font-family="Times,serif" font-size="14.00" fill="#000000">int observed</text>
</g>
<!-- ADD_INT&#45;&gt;ADD_CACHED -->
<g id="edge5" class="edge">
<title>ADD_INT-&gt;ADD_CACHED</title>
<path fill="none" stroke="#000000" d="M70.7959,-89.6785C84.7668,-76.3385 104.3553,-57.6347 120.0192,-42.6782"/>
<polygon fill="#000000" stroke="#000000" points="122.5363,-45.1141 127.3518,-35.6768 117.7022,-40.0513 122.5363,-45.1141"/>
<text text-anchor="middle" x="160.3527" y="-58.2" font-family="Times,serif" font-size="14.00" fill="#000000">non-int observed</text>
</g>
<!-- ADD_CACHED&#45;&gt;ADD_CACHED -->
<g id="edge3" class="edge">
<title>ADD_CACHED-&gt;ADD_CACHED</title>
<path fill="none" stroke="#000000" d="M214.6498,-26.0063C229.5394,-25.2234 240.3375,-22.5547 240.3375,-18 240.3375,-14.5484 234.1364,-12.1799 224.6528,-10.8943"/>
<polygon fill="#000000" stroke="#000000" points="224.9234,-7.4046 214.6498,-9.9937 224.2956,-14.3764 224.9234,-7.4046"/>
</g>
</g>
</svg>

Let's see how that looks in code.

```c
void eval_code_quickening(Frame* frame) {
  // ...
  while (true) {
    // ...
    switch (op) {
      // ...
      case ADD: {
        Object* right = pop(frame);
        Object* left = pop(frame);
        if (object_type(left) == kInt) {
          do_add_int(frame, left, right);
          code->bytecode[frame->pc] = ADD_INT;
          break;
        }
        add_update_cache(frame, left, right);
        code->bytecode[frame->pc] = ADD_CACHED;
        break;
      }
    // ...
    }
    frame->pc += kBytecodeSize;
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

A great extension here would be to add a polymorphic cache. Those are designed
to efficiently handle a small (less than five, normally) amount of repeated
types at a given point.

## Why is this faster?

Even if we leave the interpreter in this state, a small C bytecode interpreter,
we save a couple of instructions and some call overhead in the fast path of
integer addition. This is a decent win for math-heavy applications.

In the best case, though, we save a great deal of instructions. It's entirely
possible that the compiler will optimize the entire body of `ADD_INT` to
something like:

```
pop rax
pop rcx
cmp rax, $IntTag
jne slow_path
add rcx
push rcx
jmp next_opcode
slow_path:
; ...
```

It won't look exactly like that, due to our object representation and because
our `push`/`pop` functions do not operate on the C call stack, but it will be a
little closer than before. But what if we could fix these issues and trim down
the code even further?

Then we might have something like the [Dart intermediate
implementation][dart-il] of addition for small integers on x86-64. The
following C++ code emits assembly for a specialized small integer handler:

```c++
void CheckedSmiOpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  // ...
  Register left = locs()->in(0).reg();
  Register right = locs()->in(1).reg();
  // Check both left and right are small integers
  __ movq(TMP, left);
  __ orq(TMP, right);
  __ testq(TMP, compiler::Immediate(kSmiTagMask));
  __ j(NOT_ZERO, slow_path->entry_label());
  Register result = locs()->out(0).reg();
  __ movq(result, left);
  __ addq(result, right);
  // ...
}
```

This example is a little bit different since it is using an optimizing compiler
and assumes the input and output are both in registers, but still expresses the
main ideas.

See also the JVM [template interpreter implementation][jvm-ti] for binary
operations on small integers:

```c++
void TemplateTable::iop2(Operation op) {
  // ...
  __ pop_i(rdx);
  __ addl (rax, rdx);
  // ...
}
```

which pops the top of the stack and adds `rax` to it. I think this is because
the JVM caches the top of the stack in the register `rax` at all times, but I
have not been able to confirm this. It would explain why it adds `rax` and why
there is no `push`, though.

[dart-il]: https://github.com/dart-lang/sdk/blob/b1c09ecd8f30adc02f7623f6137e07a51a648dc3/runtime/vm/compiler/backend/il_x64.cc#L3549
[jvm-ti]: https://github.com/openjdk/jdk/blob/a6d950587bc68f81495660f59169b7f1970076e7/src/hotspot/cpu/x86/templateTable_x86.cpp#L1338

## Exploring further

There are a number of improvements that could be made to this very simple demo.
Bytecode rewriting can unlock a lot of performance gains with additional work.
I will list some of them below:

* Make a template interpreter like in the JVM. This will allow your specialized
  opcodes (like `ADD_INT`) directly make use of the call stack.
* Make a template JIT. This is the "next level up" from a template interpreter.
  Instead of jumping between opcode handlers in assembly, paste the assembly
  implementations of the opcodes one after another in memory. This will remove
  a lot of the interpretive dispatch overhead in the bytecode loop.
* Special case small integers in your object representation. Why allocate a
  whole object if you can fit a great deal of integers in a [tagged
  pointer][tagged-pointer]?  This will simplify some of your math and type
  checking. I wrote a [follow-up post](/blog/small-objects/) about this!

Maybe I will even write about them in the future.

[tagged-pointer]: https://bernsteinbear.com/pl-resources/#pointer-tagging-and-nan-boxing

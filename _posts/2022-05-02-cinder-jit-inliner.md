---
title: "How the Cinder JIT's inliner works"
layout: post
date: 2022-05-02
description: >
  Inlining is one of the most important compiler optimizations. This post
  describes the Cinder JIT's function inliner and how it speeds up Python code.
canonical_url: https://engineering.fb.com/2022/05/02/open-source/cinder-jits-instagram/
---

*Originally published on the [Meta Engineering Blog](https://engineering.fb.com/2022/05/02/open-source/cinder-jits-instagram/).*

Since Instagram runs one of the world's largest deployments of the Django web
framework, we have natural interest in finding ways to optimize Python so we
can speed up our production application. As part of this effort, we've recently
open-sourced [Cinder](https://github.com/facebookincubator/cinder), our Python
runtime that is a fork of CPython. Cinder includes optimizations like immortal
objects, shadowcode (our term for inline caching and quickening), [Static
Python](https://github.com/facebookincubator/cinder#static-python), and [Strict
Modules](https://github.com/facebookincubator/cinder#strict-modules). But this
blog focuses on the just-in-time (JIT) compiler and its recently released
[function
inliner](https://github.com/facebookincubator/cinder/commit/f3c50b3938906149b32c9cd36c3a41f0e898b52d).

Even with Static Python and the shadowcode enabled in the bytecode interpreter,
there is still some overhead that we can get rid of by compiling to native
code. There is overhead present in the bytecode dispatch (the switch/case), the
stack model (pushing and popping to pass objects between opcodes), and also in
a very generic interpreter---at least in CPython 3.8. So we wrote a JIT
compiler to remove a lot of this overhead.

## A bit about the JIT

_Note: If you are already familiar with Python bytecode and JITs, you might
want to skip down to [Inlining and its benefits](#inlining-and-its-benefits)._

The JIT compiles functions one at a time, translating from bytecode to a
control-flow graph (CFG), to high-level intermediate representation (HIR), to
static single assignment (SSA) HIR, to low-level intermediate representation
(LIR), to register-allocated LIR, to assembly.

Translating from bytecode to a register-based HIR removes the stack overhead.
Translating to native code removes the dispatch overhead. And several compiler
passes, including type inference, specialize the code from its previous generic
form. Let's walk through how the JIT compiles two Python functions from
beginning to end. Take a look at this Python code example:

```python
def callee(x):
    return x + 1

def caller():
    return callee(3)
```

`callee` is a very generic function. It has no idea what the type of `x` is, so
it cannot specialize the addition operation. In `caller`, the lookup for the
global `callee` must happen anew every time because in Python global variable
bindings are mutable. To get an idea of what this looks like, we can look at
the bytecode.

This Python code gets compiled to the following CPython 3.8 bytecode:

```
callee:
              0 LOAD_FAST                0 (x)
              2 LOAD_CONST               1 (1)
              4 BINARY_ADD
              6 RETURN_VALUE

caller:
              0 LOAD_GLOBAL              0 (callee)
              2 LOAD_CONST               1 (3)
              4 CALL_FUNCTION            1
              6 RETURN_VALUE
```

In this representation, produced by the `dis` module, there are four columns.
The first contains the bytecode offset (0, 2, 4, ...) inside each function. The
second contains the human-readable representation of the opcode. The third
contains the byte-wide argument to the opcode. The fourth contains the
human-readable context-aware meaning of the opcode argument. In most cases,
these are read off an auxiliary structure in the `PyCodeObject` struct and are
not present in the bytecode.

This bytecode normally gets executed in the interpreter. In the bytecode
interpreter, the function call path involves a lot of machinery to query
properties of the callee function and the call site. This machinery checks that
the argument count and parameter count match up, that defaults are filled in,
resolves `__call__` methods for nonfunctions, heap allocate a call frame, and
more. This is rather involved and not always necessary. The JIT often knows
some more information at compile time and can elide checks that it knows are
unnecessary. It also can often avoid dynamic lookup for global variables,
inline constants into the instruction stream, and use [shadow
frames](https://github.com/facebookincubator/cinder/blob/1642fffb42a3a5914386d029bc538a79c435d31b/Include/internal/pycore_shadow_frame_struct.h).
A shadow frame consists of two stack-allocated words containing metadata we can
use to reify `PyFrameObject`s. They are pushed and popped in every function
prologue and epilogue.

Before the JIT can optimize this Python code, it must be transformed from
bytecode into a control flow graph. To do this, we first discover basic block
boundaries. Jumps, returns, and `raise` terminate basic blocks, which means that
the functions above only have one basic block each. Then the JIT does abstract
interpretation on the stack-based bytecode to turn it into our infinite
register IR.

Below is the initial HIR when translated straight off the bytecode:

```
# Initial HIR
fun __main__:callee {
  bb 0 {
    v0 = LoadArg<0; "x">
    v0 = CheckVar<"x"> v0
    v1 = LoadConst<MortalLongExact[1]>
    v2 = BinaryOp<Add> v0 v1
    Return v2
  }
}

fun __main__:caller {
  bb 0 {
    v0 = LoadGlobalCached<0; "callee">
    v0 = GuardIs<0xdeadbeef> v0
    v1 = LoadConst<MortalLongExact[3]>
    v2 = VectorCall<1> v0 v1
    Return v2
  }
}
```

It has some additional information not present in the bytecode. For starters,
it has objects embedded into the instructions (with all pointers replaced with
`0xdeadbeef`). `LoadConst` is parameterized by the type `MortalLongExact[1]`,
which describes precisely the `PyObject*` for `1`. It also has this new
`GuardIs` instruction, which has an address. This is automatically inserted
after `LoadGlobalCached` and is based on the assumption that globals change
infrequently. Global variables are normally stored in a big module-scope
dictionary, mapping global names to values. Instead of reloading from the
dictionary each time, we can do a fast pointer comparison and leave the JIT
(deoptimize) if it fails.

This representation is useful but not entirely what we need. Before we can run
our other optimization passes on the HIR, it needs to be converted to SSA. So
we run the SSA pass on it, which also does basic flow typing:

```
# SSA HIR
fun __main__:callee {
  bb 0 {
    v3:Object = LoadArg<0; "x">
    v4:Object = CheckVar<"x"> v3
    v5:MortalLongExact[1] = LoadConst<MortalLongExact[1]>
    v6:Object = BinaryOp<Add> v4 v5
    Return v6
  }
}

fun __main__:caller {
  bb 0 {
    v3:OptObject = LoadGlobalCached<0; "callee">
    v4:MortalFunc[function:0xdeadbeef] = GuardIs<0xdeadbeef> v3
    v5:MortalLongExact[3] = LoadConst<MortalLongExact[3]>
    v6:Object = VectorCall<1> v4 v5
    Return v6
  }
}
```

You can see that now variable definitions are annotated with the type that the
JIT has inferred. For `LoadConst`, this is the type of the constant. For other
operations like `LoadGlobalCached`, it is the type of the global variable when
the function was compiled. Because of our assumption about the stability of
module globals, the JIT can infer function call targets and burn in addresses
to generated code (see `MortalFunc[function:0xdeadbeef]` above) after the
guard.

After SSA, the JIT will pass the HIR to the optimizer. Our current set of
optimization passes will remove the `CheckVar` (CPython guarantees arguments
will not be null), but that's about it for these two functions. We can't
optimize away the generic binary operation (`BinaryOp<Add>`) or the generic
function call (`VectorCall<1>`). So we get this:

```
# Final HIR (without inlining)
fun __main__:callee {
  bb 0 {
    v3:Object = LoadArg<0; "x">
    v5:MortalLongExact[1] = LoadConst<MortalLongExact[1]>
    v6:Object = BinaryOp<Add> v3 v5
    Return v6
  }
}

fun __main__:caller {
  bb 0 {
    v3:OptObject = LoadGlobalCached<0; "callee">
    v4:MortalFunc[function:0xdeadbeef] = GuardIs<0xdeadbeef> v3
    v5:MortalLongExact[3] = LoadConst<MortalLongExact[3]>
    v6:Object = VectorCall<1> v4 v5
    Return v6
  }
}
```

We can't optimize these generic operations away because we lack type
information. And we lack type information because the JIT is method-at-a-time
(as opposed to tracing or some kind of global optimization). Type information
and specialization happen only within a function. Additionally, functions are
compiled prefork, before they are ever run.

But what if we had more information?

## Inlining and its benefits

If we could inline the body of `callee` into `caller`, we would get more
information about the argument to `callee`. It also would remove the function
call overhead. For our code here, that is more than enough. On other code, it
has other benefits, as well, like removing inline cache pressure
(monomorphizing caches in callees) and reducing register stack spills due to
the native code calling conventions.

If we hypothetically inline `callee` into `caller` manually, it might look
something like the following:

```
# Hypothetical inlined HIR
fun __main__:caller {
  bb 0 {
    v3:OptObject = LoadGlobalCached<0; "callee">
    v4:MortalFunc[function:0xdeadbeef] = GuardIs<0xdeadbeef> v3
    v5:MortalLongExact[3] = LoadConst<MortalLongExact[3]>
    # Inlined "callee"
    v13:MortalLongExact[1] = LoadConst<MortalLongExact[1]>
    v16:Object = BinaryOp<Add> v5 v13
    # End inlined "callee"
    Return v16
  }
}
```

Now we have a lot more information about the types to `BinaryOp`. An
optimization pass can now specialize this to an opcode called `LongBinaryOp`,
which calls `int.__add__` directly:

```
# Hypothetical inlined+optimized HIR
fun __main__:caller {
  bb 0 {
    v3:OptObject = LoadGlobalCached<0; "callee">
    v4:MortalFunc[function:0xdeadbeef] = GuardIs<0xdeadbeef> v3
    v5:MortalLongExact[3] = LoadConst<MortalLongExact[3]>
    # Inlined "callee"
    v13:MortalLongExact[1] = LoadConst<MortalLongExact[1]>
    v16:LongExact = LongBinaryOp<Add> v5 v13
    # End inlined "callee"
    Return v16
  }
}
```

This lets us reason better about the memory effects of the binary operation: We
know precisely what built-in function it's calling. Or---even better---we could
constant fold it completely:

```
# Hypothetical inlined+optimized HIR II
fun __main__:caller {
  bb 0 {
    v3:OptObject = LoadGlobalCached<0; "callee">
    v4:MortalFunc[function:0xdeadbeef] = GuardIs<0xdeadbeef> v3
    # Inlined "callee"
    v17:MortalLongExact[4] = LoadConst<MortalLongExact[4]>
    # End inlined "callee"
    Return v17
  }
}
```

This is pretty neat. With one compiler pass adding more information, the other
passes reduced our function call to a constant. For now, we still need the
`LoadGlobalCached` and `GuardIs` in case somebody changes the definition of
`callee`, but they do not take much time.

Now that we have seen what inlining can do, let's take a look at the concrete
implementation inside Cinder.

## How the inliner compiler pass works

Let's go back to the original nonhypothetical optimized HIR for `caller`. The
inliner is a compiler pass that will receive HIR, which looks roughly like
this:

```
# Original HIR, pre-inlining
fun __main__:caller {
  bb 0 {
    v3:OptObject = LoadGlobalCached<0; "callee">
    v4:MortalFunc[function:0xdeadbeef] = GuardIs<0xdeadbeef> v3
    v5:MortalLongExact[3] = LoadConst<MortalLongExact[3]>
    v6:Object = VectorCall<1> v4 v5
    Return v6
  }
}
```

It iterates over all the `VectorCall`s and collects the calls for which the
target is known. In this case, `v4` is known to be a particular `function`. We
collect all the call sites ahead of time so we are not modifying the CFG as we
iterate.

Then, for each call, if the callee can be inlined, we inline the callee into
the caller. A function might not be inlinable if, for example, the arguments
don't match the parameters. This means a couple of separate steps:

**1.** Construct HIR of the target inside the caller's CFG, but keep the graphs
separate. The caller is already in SSA form, and we need to maintain that
invariant, so we SSA-ify the callee's graph separately. We don't support
running SSA on a program twice, otherwise we would probably run SSA on the
whole CFG post-inline. We also rewrite all the `Return` instructions into one
big return. This ensures that we only have one entry point into the callee and
one exit from the callee.

```
fun __main__:caller {
  bb 0 {
    v3:OptObject = LoadGlobalCached<0; "callee">
    v4:MortalFunc[function:0xdeadbeef] = GuardIs<0xdeadbeef> v3
    v5:MortalLongExact[3] = LoadConst<MortalLongExact[3]>
    v6:Object = VectorCall<1> v4 v5
    Return v6
  }

  # Non-linked callee
  bb 1 {
    v7 = LoadArg<0; "x">
    v8 = CheckVar<"x"> v7
    v9 = LoadConst<MortalLongExact[1]>
    v10 = BinaryOp<Add> v8 v9
    Return v10
  }
}
```

**2.** Split the basic block containing the call instruction after the call
instruction. For example, in our above example, split `bb 0` into:

```
fun __main__:caller {
  bb 0 {
    v3:OptObject = LoadGlobalCached<0; "callee">
    v4:MortalFunc[function:0xdeadbeef] = GuardIs<0xdeadbeef> v3
    v5:MortalLongExact[3] = LoadConst<MortalLongExact[3]>
    v6:Object = VectorCall<1> v4 v5
  }

  # Non-linked callee
  bb 1 {
    v7 = LoadArg<0; "x">
    v8 = CheckVar<"x"> v7
    v9 = LoadConst<MortalLongExact[1]>
    v10 = BinaryOp<Add> v8 v9
    Return v10
  }

  bb 2 {
    Return v6
  }
}
```

**3.** Add bookkeeping instructions and a branch instruction to the callee
constructed in step one. Remember shadow frames? We use `BeginInlinedFunction`
and `EndInlinedFunction` to push and pop shadow frames for inlined functions.
We also remove the call.

```
fun __main__:caller {
  bb 0 {
    v3:OptObject = LoadGlobalCached<0; "callee">
    v4:MortalFunc[function:0xdeadbeef] = GuardIs<0xdeadbeef> v3
    v5:MortalLongExact[3] = LoadConst<MortalLongExact[3]>
    BeginInlinedFunction
    Branch<1>
  }

  # Linked callee
  bb 1 (preds 0) {
    v7 = LoadArg<0; "x">
    v8 = CheckVar<"x"> v7
    v9 = LoadConst<MortalLongExact[1]>
    v10 = BinaryOp<Add> v8 v9
    Return v10
  }

  bb 2 {
    EndInlinedFunction
    Return v6
  }
}
```

**4.** Since `LoadArg` does not make sense for the callee---there is no
function call anymore, so no more args---rewrite it to an `Assign`. Since we
checked the arguments against the parameters earlier, we assign directly from
the register that held the argument.

```
fun __main__:caller {
  bb 0 {
    v3:OptObject = LoadGlobalCached<0; "callee">
    v4:MortalFunc[function:0xdeadbeef] = GuardIs<0xdeadbeef> v3
    v5:MortalLongExact[3] = LoadConst<MortalLongExact[3]>
    BeginInlinedFunction
    Branch<1>
  }

  # Linked callee with rewritten LoadArg
  bb 1 (preds 0) {
    v7 = Assign v5
    v8 = CheckVar<"x"> v7
    v9 = LoadConst<MortalLongExact[1]>
    v10 = BinaryOp<Add> v8 v9
    Return v10
  }

  bb 2 {
    EndInlinedFunction
    Return v6
  }
}
```

**5.** Now we rewrite the inlined `Return` to an `Assign` to the output of the
original `VectorCall` instruction. Since we only have one `Return` point and do
not need to join many of them (they have already been joined in the callee), we
can reuse the output of the call instruction.

```
fun __main__:caller {
  bb 0 {
    v3:OptObject = LoadGlobalCached<0; "callee">
    v4:MortalFunc[function:0xdeadbeef] = GuardIs<0xdeadbeef> v3
    v5:MortalLongExact[3] = LoadConst<MortalLongExact[3]>
    BeginInlinedFunction
    Branch<1>
  }

  # Linked callee with rewritten Return
  bb 1 (preds 0) {
    v7 = Assign v5
    v8 = CheckVar<"x"> v7
    v9 = LoadConst<MortalLongExact[1]>
    v10 = BinaryOp<Add> v8 v9
    v6 = Assign v10
    Branch<2>
  }

  bb 2 (preds 1) {
    EndInlinedFunction
    Return v6
  }
}
```

**6.** You will notice that despite being straight-line code, we have some
unnecessary branches. We run a `CleanCFG` pass to take care of this for us.

```
fun __main__:caller {
  bb 0 {
    v3:OptObject = LoadGlobalCached<0; "callee">
    v4:MortalFunc[function:0xdeadbeef] = GuardIs<0xdeadbeef> v3
    v5:MortalLongExact[3] = LoadConst<MortalLongExact[3]>
    BeginInlinedFunction
    v7 = Assign v5
    v8 = CheckVar<"x"> v7
    v9 = LoadConst<MortalLongExact[1]>
    v10 = BinaryOp<Add> v8 v9
    v6 = Assign v10
    EndInlinedFunction
    Return v6
  }
}
```

**7.** We have now added new untyped code to our typed CFG. To run other
optimization passes, we need to do another round of type inference and reflow
the types.

```
fun __main__:caller {
  bb 0 {
    v3:OptObject = LoadGlobalCached<0; "callee">
    v4:MortalFunc[function:0xdeadbeef] = GuardIs<0xdeadbeef> v3
    v5:MortalLongExact[3] = LoadConst<MortalLongExact[3]>
    BeginInlinedFunction
    v7:MortalLongExact[3] = Assign v5
    v8:MortalLongExact[3] = CheckVar<"x"> v7
    v9:MortalLongExact[1] = LoadConst<MortalLongExact[1]>
    v10:Object = BinaryOp<Add> v8 v9
    v6:Object = Assign v10
    EndInlinedFunction
    Return v6
  }
}
```

**8.** Now that we once more have fully typed code, we can run more
optimization passes. `CopyPropagation` will take care of the useless `Assign`s,
`Simplify` will take care of the unnecessary `CheckVar`, and then we are done!

```
fun __main__:caller {
  bb 0 {
    v3:OptObject = LoadGlobalCached<0; "callee">
    v4:MortalFunc[function:0xdeadbeef] = GuardIs<0xdeadbeef> v3
    v5:MortalLongExact[3] = LoadConst<MortalLongExact[3]>
    BeginInlinedFunction
    v9:MortalLongExact[1] = LoadConst<MortalLongExact[1]>
    v10:LongExact = LongBinaryOp<Add> v5 v9
    EndInlinedFunction
    Return v10
  }
}
```

Here we have compiled `callee` in the context of `caller`, but `callee` might
not always be inlined into its other callers. It can still be compiled as a
normal standalone function.

## What makes inlining tricky

Inlining is not just all fun and graph transformations. There are Python APIs
and profiling tools that rely on being able to have an accurate view of the
Python stack, as if the inlining never happened.

**Sampling profilers:** We have a sampling stack profiler that cannot run any
code and needs to be able to walk a chain of pointers and discover what
functions are currently running. We do this with shadow frames.

**Deoptimization metadata:** When a function raises an exception or otherwise
transfers control to the interpreter (deoptimization), it needs to materialize
a `PyFrameObject` with all the variables that should have existed at the time
(but might have been erased by the JIT), line numbers, etc. And this needs
information about what any given point in the machine code refers back to in
the Python code.

**Coroutines:** Inlining normal functions into coroutines is fine because
functions execute by starting at the top and continuing until they are
finished. We can replace a `Call` with its call target. But coroutines have to
yield control every so often and also materialize a coroutine or generator
object when called. This is a little bit trickier, and we will tackle this in
the future. Inlining functions into coroutines is not exactly tricky, but it is
more work because coroutines have a slightly different frame layout and we have
not yet modified the frame to support multiple shadow frames.

**Frame materialization outside the interpreter:** It turns out Python allows
both Python programmers and C extension developers to get a Python frame
whenever they want. For Python programmers, CPython exposes (as an
implementation detail, not a guarantee, to be fair) `sys._getframe`. For C
extension programmers and standard library developers, `PyEval_GetFrame` is
available. This means that even though there might be no deoptimization events
in the middle of an inlined function, some managed or native code might decide
to request that a frame be created anyway. And the JIT-ed code, which otherwise
would have expected the shadow frames to still be around, would also have to
handle the case where they have been replaced by real Python frames.

**When to inline:** Inlining every callee into its caller is not necessarily
the best move. Some callees never get called, and inlining them would increase
the code size. Even if they do get called, it may still make more sense to
leave the function call than to bloat the caller's code. Runtimes often have
heuristics and rules about when to inline functions, and people spend years
tuning them for different workloads to optimize performance.

**But what if someone changes the target's `__code__`?** We have a hook to
detect this from Python code. In this case, we invalidate the JIT-ed code for
that function. For inlined functions, we can either check that it hasn't
changed before every execution (slow) or react to changes by patching our
generated code. Neither of these exists yet, but they are not so hard to
implement. For native (C) extensions, we have to trust that the extensions are
well behaved and will notify us after messing with the `PyFunctionObject`
fields.

### Surprisingly not-tricky things

**What if the callee changes?** Python is a very dynamic language. Variables
can change type over time, programmers can write to global variables in other
modules, etc. But the inliner does not have to deal with this at all. The
inliner starts with the knowledge that the callee is known and works from
there. If the value changes, the guard instruction will maintain the invariant
for the native code and deoptimize into the interpreter otherwise.

## Looking forward

It's time to collect data about the performance characteristics of our workload
and figure out whether we can develop good heuristics about what functions to
inline. There are papers to read and evaluate.

Take a look at our [GitHub repo](https://github.com/facebookincubator/cinder),
and play around with Cinder. We have included a Dockerfile and [prebuilt Docker
image](https://github.com/facebookincubator/cinder/pkgs/container/cinder) to
make this easier.

## Updates

We now [constant-fold
`LongBinaryOp`](https://github.com/facebookincubator/cinder/commit/6ee3e6af7cacab4cece2b12e080e0a67d975b169)
if it has constant operands.

We have since [removed `BeginInlinedFunction` and
`EndInlinedFunction`](https://github.com/facebookincubator/cinder/commit/c4a1f4197314d59afaba61205bedb2c0919efda3)
for trivial functions that do not need shadow frames.

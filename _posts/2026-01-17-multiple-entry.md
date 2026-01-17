---
title: "A CFG design conundrum"
layout: post
---

## Background and bytecode design

The ZJIT compiler compiles Ruby bytecode (YARV) to machine code. It starts by
transforming the stack machine bytecode into a high-level graph-based
intermediate representation called HIR.

We use a more or less typical control-flow graph (CFG) in HIR. We have a
compilation unit, `Function`, which has multiple basic blocks, `Block`. Each
block contains multiple instructions, `Insn`. HIR is always in SSA form, and we
use the variant of SSA with block parameters.

Where it gets weird, though, is our handling of multiple entrypoints. See, YARV
handles default positional parameters (but *not* default keyword parameters) by
embedding the code to compute the positionals inside the bytecode. Then callers
are responsible for figuring out what offset in the bytecode they should start
running the callee, depending on the amount of arguments provided. (Keyword
parameters have explicit presence checks in the callee because they are passed
in un-ordered.)

In the following example, we have a function that takes two optional positional
parameters `a` and `b`. If neither is provided, we start at offset `0000`. If
just `a` is provided, we start at offset `0005`. If both are provided, we can
start at offset `0010`.

```
$ ruby --dump=insns -e 'def foo(a=compute_a, b=compute_b) = a + b'
...
== disasm: #<ISeq:foo@-e:1 (1,0)-(1,41)>
local table (size: 2, argc: 0 [opts: 2, rest: -1, post: 0, block: -1, kw: -1@-1, kwrest: -1])
[ 2] a@0<Opt=0> [ 1] b@1<Opt=5>
0000 putself                                                          (   1)
0001 opt_send_without_block   <calldata!mid:compute_a, argc:0, FCALL|VCALL|ARGS_SIMPLE>
0003 setlocal_WC_0            a@0
0005 putself
0006 opt_send_without_block   <calldata!mid:compute_b, argc:0, FCALL|VCALL|ARGS_SIMPLE>
0008 setlocal_WC_0            b@1
0010 getlocal_WC_0            a@0[Ca]
0012 getlocal_WC_0            b@1
0014 opt_plus                 <calldata!mid:+, argc:1, ARGS_SIMPLE>[CcCr]
0016 leave                    [Re]
$
```

Unlike in Python, where default arguments are evaluated *at function creation
time*, Ruby computes the default values *at function call time*. For this
reason, embedding the default code inside the callee makes a lot of sense; we
have a full call frame already set up, so any exception handling machinery
or profiling or ... doesn't need special treatment.

Since the caller knows what arguments it is passing, and often to what
function, we can efficiently support this in the JIT. We just need to know what
offset in the compiled callee to call into. The interpreter can also call into
the compiled function, which just has a stub to do dispatch to the appropriate
entry block.

This has led us to design the HIR to support *multiple function entrypoints*.
Instead of having just a single entry block, as most control-flow graphs do,
each of our functions now has an array of function entries: one for the
interpreter, at least one for the JIT, and more for default parameter handling.

```
HIR example
```

Each entry block also comes with block parameters which mirror the function's
parameters. These get passed in (roughly) the System V ABI registers.

This is kind of gross. We have to handle these blocks specially in reverse
post-order (RPO) graph traversal. And, recently, I ran into an even worse case
when trying to implement the Cooper-style "engineered" dominator algorithm: if
we walk backwards in block dominators, the walk is not guaranteed to be
confluent (TODO right word?). All non-entry blocks are dominated by all entry
blocks, which are only dominated by themselves. There is no one "start block".
So what is there to do?

## The design conundrum

**Approach 1** is to keep everything as-is, but handle entry blocks specially
in the dominator algorithm too. I'm not exactly sure what would be needed, but
it seems possible. Most of the existing block infra could be left alone, but
it's not clear how much this would "spread" within the compiler. What else in
the future might need to be handled specially?

**Approach 2** is to synthesize a super-entry block and make it a predecessor
of every interpreter and JIT entry block. Inside this approach there are two
ways to do it: one (**2.a**) is to fake it and report some non-existent block.
Another (**2.b**) is to actually make a block and a new instruction that is a
quasi-jump instruction. In this approach, we would either need to synthesize
fake block arguments for the JIT entry block parameters or add some kind of new
`LoadArg<i>` instruction that reads the argument *i* passed in.

(suggested by Iain Ireland, as seen in the IBM COBOL compiler)

**Approach 3** is to duplicate the entire CFG per entrypoint. This would return
us to having one entry block per CFG at the expense of code duplication. It
handles the problem pretty cleanly but then *forces* code duplication. I think
I want the duplication to be opt-in instead of having it be the only way we
support multiple entrypoints. What if it increases memory too much? The
specialization probably would make the generated code faster, though.

(suggested by Ben Titzer)

None of these approaches feel great to me. The probable candidate is **2.b**
where we have `LoadArg` instructions. That gives us flexibility to also later
add full specialization without forcing it.

What does your compiler do?

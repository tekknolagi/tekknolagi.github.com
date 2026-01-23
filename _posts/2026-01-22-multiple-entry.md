---
title: "A multi-entry CFG design conundrum"
layout: post
---

## Background and bytecode design

The ZJIT compiler compiles Ruby bytecode (YARV) to machine code. It starts by
transforming the stack machine bytecode into a high-level graph-based
intermediate representation called HIR.

We use a more or less typical[^ebb] control-flow graph (CFG) in HIR. We have a
compilation unit, `Function`, which has multiple basic blocks, `Block`. Each
block contains multiple instructions, `Insn`. HIR is always in SSA form, and we
use the variant of SSA with block parameters instead of phi nodes.

[^ebb]: We use extended basic blocks (EBBs), but this doesn't matter for this
    post. It makes dominators and predecessors slightly more complicated (now
    you have dominating *instructions*), but that's about it as far as I can
    tell. We'll see how they fare in the face of more complicated analysis
    later.

Where it gets weird, though, is our handling of multiple entrypoints. See, YARV
handles default positional parameters (but *not* default keyword parameters) by
embedding the code to compute the defaults inside the callee bytecode. Then
callers are responsible for figuring out what offset in the bytecode they
should start running the callee, depending on the amount of arguments the
caller provides. (Keyword parameters have explicit presence checks in the
callee because they are passed in un-ordered.)

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

(See the jump table debug output: `[ 2] a@0<Opt=0> [ 1] b@1<Opt=5>`)

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
Each of these entry blocks is separately callable from the outside world.

Here is what the (slightly cleaned up) HIR looks like for the above example:

```
Optimized HIR:
fn foo@tmp/branchnil.rb:4:
bb0():
  EntryPoint interpreter
  v1:BasicObject = LoadSelf
  v2:BasicObject = GetLocal :a, l0, SP@5
  v3:BasicObject = GetLocal :b, l0, SP@4
  v4:CPtr = LoadPC
  v5:CPtr[CPtr(0x16d27e908)] = Const CPtr(0x16d282120)
  v6:CBool = IsBitEqual v4, v5
  IfTrue v6, bb2(v1, v2, v3)
  v8:CPtr[CPtr(0x16d27e908)] = Const CPtr(0x16d282120)
  v9:CBool = IsBitEqual v4, v8
  IfTrue v9, bb4(v1, v2, v3)
  Jump bb6(v1, v2, v3)
bb1(v13:BasicObject):
  EntryPoint JIT(0)
  v14:NilClass = Const Value(nil)
  v15:NilClass = Const Value(nil)
  Jump bb2(v13, v14, v15)
bb2(v27:BasicObject, v28:BasicObject, v29:BasicObject):
  v65:HeapObject[...] = GuardType v27, HeapObject[class_exact*:Object@VALUE(0x1043aed00)]
  v66:BasicObject = SendWithoutBlockDirect v65, :compute_a (0x16d282148)
  Jump bb4(v27, v66, v29)
bb3(v18:BasicObject, v19:BasicObject):
  EntryPoint JIT(1)
  v20:NilClass = Const Value(nil)
  Jump bb4(v18, v19, v20)
bb4(v38:BasicObject, v39:BasicObject, v40:BasicObject):
  v69:HeapObject[...] = GuardType v38, HeapObject[class_exact*:Object@VALUE(0x1043aed00)]
  v70:BasicObject = SendWithoutBlockDirect v69, :compute_b (0x16d282148)
  Jump bb6(v38, v39, v70)
bb5(v23:BasicObject, v24:BasicObject, v25:BasicObject):
  EntryPoint JIT(2)
  Jump bb6(v23, v24, v25)
bb6(v49:BasicObject, v50:BasicObject, v51:BasicObject):
  v73:Fixnum = GuardType v50, Fixnum
  v74:Fixnum = GuardType v51, Fixnum
  v75:Fixnum = FixnumAdd v73, v74
  CheckInterrupts
  Return v75
```

If you're not a fan of text HIR, here is an embedded clickable visualization of
HIR thanks to our former intern [Aiden](https://aidenfoxivey.com/) porting
Firefox's [Iongraph](https://github.com/mozilla-spidermonkey/iongraph):

<iframe width="100%" height="400" src="/assets/zjit-multi-entry-iongraph.html"></iframe>

(You might have to scroll sideways and down and zoom around. Or you can [open it
in its own window](/assets/zjit-multi-entry-iongraph.html).)

Each entry block also comes with block parameters which mirror the function's
parameters. These get passed in (roughly) the System V ABI registers.

This is kind of gross. We have to handle these blocks specially in reverse
post-order (RPO) graph traversal. And, recently, I ran into an even worse case
when trying to implement the Cooper-style "engineered" dominator algorithm: if
we walk backwards in block dominators, the walk is not guaranteed to converge.
All non-entry blocks are dominated by all entry blocks, which are only
dominated by themselves. There is no one "start block". So what is there to do?

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

Cameron Zwarich also notes that this this is an analogue to the common problem
people have when implementing the reverse: postdominators. This is because
often functions have multiple return IR instructions. He notes the usual
solution is to transform them into branches to a single return instruction.

Do you have this problem? What does your compiler do?

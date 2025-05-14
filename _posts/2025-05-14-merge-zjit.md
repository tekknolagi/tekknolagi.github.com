---
title: "ZJIT has been merged into Ruby"
layout: post
canonical_url: "https://railsatscale.com/2025-05-14-merge-zjit/"
---

Following [Maxime's presentation at RubyKaigi 2025][rubykaigi], the Ruby
developers meeting, and [Matz-san's approval][zjit-redmine], ZJIT has been
merged into Ruby. Hurray! In this post, we will give a high-level overview of
the project, which is very early in development.

[zjit-redmine]: https://bugs.ruby-lang.org/issues/21221

ZJIT is a new just-in-time (JIT) Ruby compiler built into the reference Ruby
implementation, [YARV][yarv], by the same compiler group that brought you YJIT.
We (Maxime Chevalier-Boisvert, Takashi Kokubun, Alan Wu, Max Bernstein, and
Aiden Fox Ivey) have been working on ZJIT since the beginning of this year.

[yarv]: https://en.wikipedia.org/wiki/YARV
[rubykaigi]: https://www.slideshare.net/slideshow/zjit-building-a-next-generation-ruby-jit/278807093

It's different from YJIT in several ways:

* Instead of compiling YARV bytecode directly to the low-level IR (LIR), it
  uses an high-level SSA-based intermediate representation (HIR)
* Instead of compiling one basic block at a time, it compiles one entire method
  at a time
* Instead of using [lazy basic block versioning (LBBV)][lbbv] to profile types,
  it reads historical type information from the profiled interpreter
* Instead of doing optimizations while lowering YARV to LIR, it has a
  high-level modular optimizer that works on HIR

[lbbv]: https://arxiv.org/abs/1411.0352

The main difference is that the team is making the intentional choice to build
a more traditional "textbook" compiler so it is easy for the community to
contribute to.

There are some interesting tradeoffs. For example, while YJIT's architecture
allows for easy interprocedural type-based specialization, ZJIT's architecture
gives more code at once to the optimizer.

Let's talk about the ZJIT architecture.

## Current architecture

At a high level, ZJIT takes in YARV bytecode, builds an IR, does some
optimizations, and emits machine code. Simplified, it looks like this:

<figure>
<object data="/assets/img/zjit-pipeline.svg" type="image/svg+xml"></object>
<figcaption>How Ruby code flows through the compiler.</figcaption>
</figure>

We'll take the following sample Ruby program and bring it through the full
compiler pipeline:

```ruby
# add.rb
def add(left, right)
  left + right
end

p add(1, 2)
p add(3, 4)
```

Let's start by getting a feel for YARV.

### YARV

The Ruby VM has compiled two functions to YARV bytecode. First we see the
top-level function (omitted for brevity with `...`) and then the `add`
instruction sequence (ISEQ). There's a lot going on but it's important to note
that YARV is a [stack machine][stack-machine] with local variables. Most
instructions pop their inputs from the stack and push their results onto the
stack.

[stack-machine]: https://en.wikipedia.org/wiki/Stack_machine

For example, in `add`, `getlocal_WC_0` (offsets `0000` and `0002`) is a
specialized instruction that reads the `left` local from slot 0 (see the third
column) and pushes it onto the stack. Same with `right` at slot 1. Then it
calls into a specialized `+` handler, `opt_plus` (offset `0004`), which reads
the arguments off the stack and pushes the result back onto the stack.

```console?prompt=$
$ ruby --dump=insns add.rb
...

== disasm: #<ISeq:add@add.rb:2 (2,0)-(4,3)>
local table (size: 2, argc: 2 [opts: 0, rest: -1, post: 0, block: -1, kw: -1@-1, kwrest: -1])
[ 2] left@0<Arg>[ 1] right@1<Arg>
0000 getlocal_WC_0                          left@0                    (   3)[LiCa]
0002 getlocal_WC_0                          right@1
0004 opt_plus                               <calldata!mid:+, argc:1, ARGS_SIMPLE>[CcCr]
0006 leave
$
```

The opcode `opt_plus` is a generic method lookup and call operation but it also
has several fast-path cases inlined into the VM's instruction handler. It has
code for handling the common case of adding two small integers (fixnums) with a
fallback to the generic send.

```c
static VALUE
vm_opt_plus(VALUE recv, VALUE obj)
{
    // fast path for fixnum + fixnum
    if (FIXNUM_2_P(recv, obj) &&
        BASIC_OP_UNREDEFINED_P(BOP_PLUS, INTEGER_REDEFINED_OP_FLAG)) {
        return rb_fix_plus_fix(recv, obj);
    }
    // ... some other common cases like float + float ...
    // ... fallback code for someone having redefined `Integer#+` ...
}
```

Importantly, it's not enough for `opt_plus` to check the types of its
arguments. The opcode handler also has to make sure (via a "bop check"[^bop])
that `Integer#+` has not been redefined. If the `Integer#+` has been changed,
it must fall back to the generic operation so the VM can call into the newly
redefined method.

[^bop]: This is because it's possible to redefine (almost?) any method in Ruby,
    including built-in ones such as `Integer#+` ("**b**asic **op**erations",
    hence "bop").

    The developers of the YARV VM want to include shortcuts for common
    operations such as adding small numbers ("fixnums"), but want to still
    support falling back to very dynamic behavior.

After running the bytecode function some number of times in the interpreter (a
configurable number), ZJIT will change some opcodes to modified versions that
profile their arguments. For example, `opt_plus` will get rewritten to
`zjit_opt_plus`. This modified version records the types of the opcode's input
values on the stack into a special location that ZJIT knows about.

After a more calls to the function (some other configurable number), ZJIT will
compile it. Let's see what happens to `opt_plus` in HIR, the first part of the
compiler pipeline. If you're following along, from here on out, you'll need to
have a Ruby configured with `--enable-zjit` (see [the docs][zjit-doc]).

[zjit-doc]: https://github.com/ruby/ruby/blob/master/doc/zjit.md

### HIR

In the bytecode, which is tersely encoded, jumps are offsets, some control-flow
is implicit, and most dataflow is via the stack.

By contrast, HIR looks more like a graph. Jumps have pointers to their targets
and there's no stack: instructions that use data have pointers directly to the
instructions that create the data.

Every function has a list of basic blocks. Every basic block has a list of
instructions. Every instruction is addressable by its ID (`InsnId`, looks like
`v12`), has a type (after the `InsnId`), has an opcode, and has some operands.

To show what I mean, here is the text representation of the HIR constructed
directly from the bytecode (note how ZJIT has to be enabled with `--zjit`):


```console?prompt=$
$ ruby --zjit --zjit-dump-hir-init add.rb
HIR:
fn add:
bb0(v0:BasicObject, v1:BasicObject):
  v4:BasicObject = SendWithoutBlock v0, :+, v1
  Return v4
$
```

The HIR text representation may on the surface look similar to the bytecode but
that's only really because they are both text. A more accurate depiction of
HIR's graph-like nature is this diagram:

<figure>
<object width="400px" data="/assets/img/zjit-graph.svg" type="image/svg+xml"></object>
<figcaption>
Arrows indicate pointers from uses to the data used. Many instructions such as
<code>SendWithoutBlock</code> and <code>Send</code> produce output data. We
refer to this output data by the name of the instruction that produced it. This
is why the <code>Return</code> instruction points to the <code>Send</code>.
</figcaption>
</figure>

(We also see that `opt_plus` has been turned back into a generic method send to
`:+`. This is because (as mentioned earlier) under the hood, many `opt_xyz`
instructions such as `opt_plus` are `opt_send_without_block` in disguise and we
do our type optimization later in the compiler pipeline.)

Let's pick apart this example:

* `v4:BasicObject = SendWithoutBlock v0, :+, v1` is an instruction
* `v4` is the both the ID of the instruction and name of its output data
* The output has type `BasicObject` (or subclass)
* It is a send operation, specifically `opt_send_without_block`
* The self is `v0`
* The method name is `:+`
* The operands are both `v0` and `v1`

Then the HIR goes through our optimization pipeline.

After some optimization, the HIR looks quite different. We no longer see a
generic send operation but instead see type-specialized code:

```console?prompt=$
$ ruby --zjit --zjit-dump-hir add.rb
HIR:
fn add:
bb0(v0:BasicObject, v1:BasicObject):
  PatchPoint BOPRedefined(INTEGER_REDEFINED_OP_FLAG, BOP_PLUS)
  v7:Fixnum = GuardType v0, Fixnum
  v8:Fixnum = GuardType v1, Fixnum
  v9:Fixnum = FixnumAdd v7, v8
  Return v9
$
```

The optimizer has inserted `GuardType` instructions that each check at run-time
if their operands are `Fixnum`. If it is not a fixnum, the generated code will
jump into the interpreter as a fallback. This way, we only need to generate
specialized code---the `FixnumAdd`.

But this talk of `GuardType` and `FixnumAdd` is still pretty symbolic and
high-level. Let's go one step further in the compiler pipeline into LIR.

### LIR

LIR is meant to be a multi-platform assembler. The only fancy feature it really
provides is a register allocator. When we transform HIR into LIR, we mostly
focus on transforming our high-level operations into an assembly-like language.
To make this easier, we allocate as many virtual LIR registers as we like. Then
the register allocator maps those onto physical registers and stack locations.

Here's the `add` function in LIR:

```console?prompt=$
$ ruby --zjit --zjit-dump-lir add.rb
LIR:
fn add:
Assembler
    000 Label() -> None
    001 FrameSetup() -> None
    002 LiveReg(A64Reg { num_bits: 64, reg_no: 0 }) -> Out64(0)
    003 LiveReg(A64Reg { num_bits: 64, reg_no: 1 }) -> Out64(1)
# The first GuardType
    004 Test(Out64(0), 1_u64) -> None
    005 Jz() target=SideExit(FrameState { iseq: 0x1049ca480, insn_idx: 4, pc: 0x6000002b2520, stack: [InsnId(0), InsnId(1)], locals: [InsnId(0), InsnId(1)] }) -> None
# The second GuardType
    006 Test(Out64(1), 1_u64) -> None
    007 Jz() target=SideExit(FrameState { iseq: 0x1049ca480, insn_idx: 4, pc: 0x6000002b2520, stack: [InsnId(0), InsnId(1)], locals: [InsnId(0), InsnId(1)] }) -> None
# The FixnumAdd; side-exit if it overflows Fixnum
    008 Sub(Out64(0), 1_i64) -> Out64(2)
    009 Add(Out64(2), Out64(1)) -> Out64(3)
    010 Jo() target=SideExit(FrameState { iseq: 0x1049ca480, insn_idx: 4, pc: 0x6000002b2520, stack: [InsnId(0), InsnId(1)], locals: [InsnId(0), InsnId(1)] }) -> None
    011 Add(A64Reg { num_bits: 64, reg_no: 19 }, 38_u64) -> Out64(4)
    012 Mov(A64Reg { num_bits: 64, reg_no: 19 }, Out64(4)) -> None
    013 Mov(Mem64[Reg(20) + 16], A64Reg { num_bits: 64, reg_no: 19 }) -> None
    014 FrameTeardown() -> None
    015 CRet(Out64(3)) -> None
$
```

It is much more explicit about what is going on than the HIR. In it we see some
lower-level details such as:

* `FrameSetup` and `FrameTeardown`, which correspond to native frame base
  pointer operations
* `Test`, which is a single bit test instruction
* Explicit conditional jumps to side-exit into the interpreter, such as `Jz`
  and `Jo`
* `Sub` and `Add`, which are single math instructions

In LIR output for other functions, we might see some high-level HIR
constructions turned into calls to C runtime (helper) functions.

It's not noticeable in this example, but another difference between HIR and LIR
is that LIR is one big linear block: unlike HIR, it does not have multiple basic blocks.

Last, from LIR, we go to assembly.

### Assembly (ASM)

The assembly listing is a little bit long for the blog post but I will show an
interesting snippet that illustrates the utility of `GuardType` and
`FixnumAdd`:

```console?prompt=$
$ ruby --zjit --zjit-dump-disasm add.rb
...
# Insn: v7 GuardType v0, Fixnum
0x6376b7ad400f: test dil, 1
0x6376b7ad4013: je 0x6376b7ad4000
# Insn: v8 GuardType v1, Fixnum
0x6376b7ad4019: test sil, 1
0x6376b7ad401d: je 0x6376b7ad4005
# Insn: v9 FixnumAdd v7, v8
0x6376b7ad4023: sub rdi, 1
0x6376b7ad4027: add rdi, rsi
0x6376b7ad402a: jo 0x6376b7ad400a
...
$
```

You can see that `GuardType` and `FixnumAdd` only require a couple of very fast
machine instructions each. This is the value of type specialization!

This assembly snippet shows x86 instructions, but ZJIT also has an ARM backend.
The generated code looks very similar in ARM.

## Future plans and conclusion

It is still very early in the ZJIT project. While we encourage reading the
source and trying local experiments, we are not yet running ZJIT in production
and caution you to also avoid doing so. It is an exciting and bumpy road ahead!

For this reason, we will continue maintaining YJIT for now and Ruby 3.5 will
ship with both YJIT and ZJIT. In parallel, we will improve ZJIT until it is on
par (features and performance) with YJIT.

We're currently working on a couple more features that will make the JIT run
real code. For starters, we are implementing side-exits. Right now, if
`GuardType` observes a type that it doesn't expect, it aborts. Ideally we would
be able to actually jump into the interpreter.

Side exits will enable us to do two exciting things:

1. Run the Ruby test suite, giving us a correctness baseline
1. Run yjit-bench and other production applications, giving us a performance baseline

Then we'll get to work on profiling and seeing what optimizations are most
impactful.

Thanks for reading this post! We'll make more information and documentation
available soon.

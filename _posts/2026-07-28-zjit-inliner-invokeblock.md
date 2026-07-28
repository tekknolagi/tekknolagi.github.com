---
title: The inliner is yielding benefits for ZJIT
layout: post
canonical_url: "https://railsatscale.com/2026-07-28-the-inliner-is-yielding-benefits/"
---

*Originally published on [Rails At Scale]({{ page.canonical_url }}).*

We recently enabled a really cool feature in ZJIT that makes it feel like a
Real Compiler&trade;: the inliner! We'll write more about it soon. In this
post, we'll talk about one excellent concrete benefit we are already seeing and
how it optimizes blocks in pretty much every Ruby program.

I'll start off with a refresher on how blocks work in the Ruby interpreter,
then show you how ZJIT understands and optimizes that bytecode, and then show
you the impact of the inliner.

## Ruby: a refresher

In the beginning, there were loops.

```ruby
arr = [1, 2, 3]
i = 0
sum = 0
while i < arr.length
  sum += arr[i]
  i += 1
end
```

People used them to navigate and manipulate variable-length structures, like
arrays and strings. This was fine.

Then, in the 1970s, a small group of computer scientists at Palo Alto Research
Center invented a programming language called Smalltalk. One of the core
features of Smalltalk was that everything was an object and computation was
done by sending messages to objects.

This meant that iteration wouldn't do at all. Instead, we would have to send
the `do:` message to the array object and pass it a block object.

```smalltalk
#(1 2 3) do: [:a | sum := sum + a].
```

Then, in the 1990s, Matz, inspired by Smalltalk and Perl, created Ruby. We
still have "normal" loops but we also have a very Smalltalk-y way of doing it,
too:

```ruby
arr = [1, 2, 3]
sum = 0
arr.each do |a|
  sum += a
end
```

When this program gets compiled to Ruby bytecode, it ends up looking like a
mostly normal method call to `each` except that we pass a special kind of
argument to it: a block argument.

To see how this works inside CRuby, we're going to look at a listing of YARV
bytecode---CRuby bytecode. For more on YARV, I recommend Kevin Newton's
excellent [Advent of
YARV](https://kddnewton.com/2022/11/30/advent-of-yarv-part-0.html).

Ignore most of the bytecode dump below except for the instruction at `0009`,
the `send` instruction. We are *send*-ing (see? a message!) `each` with the
block argument `block in <main>` (passed a different way than "normal"
arguments, hence the `argc:0`).

```
$ ruby --dump=insns /tmp/arrayeach.rb
== disasm: #<ISeq:<main>@/tmp/arrayeach.rb:1 (1,0)-(5,3)>
local table (...)
[ 2] arr@0      [ 1] sum@1
0000 duparray                   [1, 2, 3]                 (   1)[Li]
0002 setlocal_WC_0              arr@0
0004 putobject_INT2FIX_0_                                 (   2)[Li]
0005 setlocal_WC_0              sum@1
0007 getlocal_WC_0              arr@0                     (   3)[Li]
0009 send                       <calldata!mid:each, argc:0>, block in <main>
0012 leave
...continued below...
```

This `block in <main>` from the bytecode listing above is the generated name of
the *instruction sequence* (bytecode) corresponding to the block we passed to
`each`. Its code, shown below, is the next thing in the bytecode dump. You can
see the usage of local variables `sum` and `a` via `getlocal` and `setlocal`
variants and also `opt_plus`, which represents addition (`+`). Again, the
details are not terribly important:

```
...continued from above...
== disasm: #<ISeq:block in <main>@/tmp/arrayeach.rb:3 (3,9)-(5,3)>
local table (...)
[ 1] a@0<AmbiguousArg>
0000 getlocal_WC_1              sum@1                     (   4)[LiBc]
0002 getlocal_WC_0              a@0
0004 opt_plus                   <calldata!mid:+, argc:1, ARGS_SIMPLE>[CcCr]
0006 dup
0007 setlocal_WC_1              sum@1
0009 leave                                                (   5)[Br]
$
```

To make this work, `Array` has an `each` method. This `each` method takes in
its optional block argument and calls it once for every element in the array.

As with most of the core data structures, `Array`'s methods tend to be written
in C and `each` is no different. Here is its nice and short definition with
comments added by me:

```c
VALUE
rb_ary_each(VALUE ary)
{
    long i;
    // Some debug-mode-only checks
    ary_verify(ary);
    // If we didn't pass a block in, return an enumerator object
    RETURN_SIZED_ENUMERATOR(ary, 0, 0, ary_enum_length);
    // We passed a block in, so call it for every element
    for (i=0; i<RARRAY_LEN(ary); i++) {
        rb_yield(RARRAY_AREF(ary, i));
    }
    return ary;
}
```

You can't really see the block parameter to the C code because it's passed in a
special location on the Ruby VM's own stack called the *block handler*. All you
need to know is that `rb_yield` knows where to find that and how to call it.

There's just one more thing, which is&hellip; hang on, weren't we building a JIT to
optimize Ruby code? How are we going to optimize this C code?

## JITs: a refresher

Rewriting code from C to Ruby means that the JIT compiler gets a chance to
introspect the run-time behavior and code. This means that, over time, as the
compiler and the runtime system grow together, more and more code might get
rewritten in Ruby. This started happening a couple of years ago with YJIT.

YJIT precipitated some interesting changes to Ruby VM internals. For example,
in 2022, `Array#each` being written in C started to hurt: it was an opaque blob
that the JIT couldn't reason about. So Kokubun submitted [a
PR](https://github.com/ruby/ruby/pull/6687) to rewrite it in Ruby. After some
back and forth, in 2024, Kokubun landed [a different
PR](https://github.com/ruby/ruby/pull/11955) that everyone was happy with.

```ruby
class Array
  def each # :nodoc:
    Primitive.attr! :inline_block, :c_trace, :without_interrupts

    unless defined?(yield)
      return Primitive.cexpr! 'SIZED_ENUMERATOR(self, 0, 0, ary_enum_length)'
    end
    i = 0
    until Primitive.rb_jit_ary_at_end(i)
      yield Primitive.rb_jit_ary_at(i)
      i = Primitive.rb_jit_fixnum_inc(i)
    end
    self
  end
end
```

Ah, finally. A version that JITs can reason about.

I keep saying "reason about" and what that means concretely here is a) that
it's written in a format that the JIT can ingest and optimize: Ruby, and b) mostly a
brief rehashing of the key lessons in the venerable Smalltalk (!) paper
[Efficient Implementation of the Smalltalk-80
System](https://dl.acm.org/doi/pdf/10.1145/800017.800542) (PDF):

* Though systems may offer very dynamic behavior, people don't frequently make
  use of wild features all over the place
  * Most people pass fewer than 4 types of objects through a given method
  * As a corollary, even if there are many classes in a system, code locality
    is super important, and we can take advantage of that
  * Also, most people do not define, re-define, and otherwise continuously
    modify method definitions

(And if you don't believe me that the lessons still apply, check out the
excellent paper [Who You Gonna
Call](https://dl.acm.org/doi/pdf/10.1145/3563834.3567538) (PDF) by Sophie
Kaleba, Octave Larose, Richard Jones, and Stefan Marr.)

So JITs like to watch what types of objects flow through methods before
compiling them. And JITs like to, since they know the types of objects, cache
method lookups and specialize method invocations on those objects.

For example, take a look at this code:

```ruby
def next_capacity(x)
  x.length * 2 + 1
end
```

`x` could be anything. There is no way of knowing its type by looking at the
code. This means that the `length` method could be anything. Furthermore, there
is no way of knowing what the return type of `x.length` is, so we can't
specialize the method lookups or invocations of `*` or `+` either.

But! Per our lessons above, likely only a few types flow through this code. Say
the JIT's profiler notices that `x` has historically been an `Array`. Then we
might reasonably assume that it will continue to be an `Array`, so when we
compile the method, we add a run-time type check: if the type is no longer an
`Array`, jump back into the interpreter.

Let's see what optimized code ZJIT can construct by combining profiling
information with the above bytecode. ZJIT operates on its own *high-level
intermediate representation* called, uncreatively, HIR. In the following HIR
snippet, we can see this very run-time type check ("guard") for the `Array`
class:

```
fn next_capacity@file.rb:8:
...
bb3(v9:BasicObject, v10:BasicObject):  # v9 is self and v10 is x
  PatchPoint NoSingletonClass(Array@0xdecaf1)
  PatchPoint MethodRedefined(Array@0xdecaf2, length@0xdecaf3, cme:0xdecaf4)
  v35:ArrayExact = GuardType v10, ArrayExact recompile
  v36:CInt64 = ArrayLength v35
  v37:Fixnum = BoxFixnum v36
  # ...
```

(with real pointers replaced by fake ones for readability)

Because classes are, among other things, collections of methods, this type
information tells us what the call target of `x.length` is: it's
`Array#length`! We have a special fast code snippet to read an array's length
so we do that instead of a method call.

And in case the methods ever get changed out from underneath us, we leave
behind these markers called `PatchPoint` that invalidate the code.

Finally, because we know that the result of `Array#length` is always a small
integer (`Fixnum`), we can special case the method lookups for `*` and `+` as
well.

```
fn next_capacity@file.rb:8:
...
bb3(v9:BasicObject, v10:BasicObject):  # v9 is self and v10 is x
  PatchPoint NoSingletonClass(Array@0xdecaf)
  PatchPoint MethodRedefined(Array@0xdecaf, length@0xdecaf, cme:0xdecaf)
  v35:ArrayExact = GuardType v10, ArrayExact recompile
  v36:CInt64 = ArrayLength v35
  v37:Fixnum = BoxFixnum v36
  v18:Fixnum[2] = Const Value(2)
  PatchPoint MethodRedefined(Integer@0xdecaf, *@0xdecaf, cme:0xdecaf)
  v41:Fixnum = FixnumMult v37, v18
  v23:Fixnum[1] = Const Value(1)
  PatchPoint MethodRedefined(Integer@0xdecaf, +@0xdecaf, cme:0xdecaf)
  v45:Fixnum = FixnumAdd v41, v23
  CheckInterrupts
  Return v45
```

There you have it. This is how JITs work: observe, assume, specialize. So why
am I telling you all this? How does this circle back to blocks?

Well, blocks work not quite the same way, but similarly. Instead of having an
object that we call a method on, we just have the target *instruction
sequence*[^procs-and-such]. But if we observe that the block argument, in its
special location, has been consistently one object, we can specialize the call
to it.

[^procs-and-such]: Mostly. I am glossing over `nil`, procs, ifuncs, etc. But
    the common path is by and away iseq blocks.

This is more or less fine for some cases of code. For example, in the following
code snippet, we only have one caller to `custom_iterator`, so its profiled
block will be *monomorphic* (one observed shape).

```ruby
def custom_iterator
  yield 1
  yield 2
  yield 3
end

custom_iterator do |x|
  puts x
end
```

This reinforces what we know and love about the Smalltalk-80 paper: code
locality wins! Yes!

But unfortunately this falls apart when we start thinking about all of the
core library methods (and potentially the methods and classes you have
stashed away in the grab-bag `utils.rb` in your application). Those methods are
probably *megamorphic* (many many observed shapes). They probably see all sorts
of stuff because they are general-purpose utilities that everybody needs, all
the time.

One such example in the Ruby core library is the venerable `Array#each`
that we saw earlier. Because there are a million different call sites to
`Array#each` across your application and each probably passes a totally
different block, we're in an unhappy situation. How can we possibly optimize
for so many different blocks? What happened to our code locality? How do we fix
this?

## The code locality is in another tower

It's okay. Code locality still rules. Look at all the various callers of
`Array#each`. They all pass a different, but constant[^not-always-constant],
block at the call-site:

[^not-always-constant]: It's not always the case that these methods are called
    with a constant block iseq. Sometimes they are called with the `&:symbol`
    form, for example. Or with the `&some_block_variable` form. In that case,
    we can use an inline cache to (with a guard) make it constant once more.
    But we have not implemented that yet, because it is rarer.

```ruby
def method_a(some_array)
  some_array.each do |x|
    puts x
  end
end

def method_b(some_array)
  some_array.each do |x|
    puts x*2
  end
end

def method_c(some_array)
  some_array.each do |x|
    puts x*4
  end
end
```

So the code locality that we need in `Array#each` to optimize the code is one
level up at the caller. If we can use that *call context*, we can specialize
the code.

YJIT accomplishes this by *splitting*, which is a very natural transformation
for basic block versioning and tracing. Such compilers are good at following
code paths as they would be executed and putting together context across method
calls. However, YJIT's heuristic for splitting blocks is based on
manual annotations: it will only kick in for certain Ruby library functions
specially annotated with `Primitive.attr! :inline_block` (and the name is a bit
of a misnomer).

The team that builds ZJIT, a method JIT, decided not to add splitting
facilities. We could split methods (and blocks), but we have an easier time
reasoning about larger code units than YJIT does because we optimize an entire
method at once. So instead, ZJIT chooses to get call context by *inlining*.

## Inlining

Method inlining refers to copying the body of the callee into the caller. In
the above example, it means copying the body of `Array#each` into each of
`method_a`, `method_b`, and `method_c`. I don't mean the Ruby code and I don't
mean the bytecode: I mean the HIR. ZJIT does this by building the HIR of the
callee (`Array#each`) into the existing HIR of the caller (`method_a`, &hellip;).

The illustrious [Kevin Menard](/authors/kevin-menard/) wrote ZJIT's inliner and
he'll write a post about all the details soon. It's pretty interesting stuff.

For now, we can take a look at the (lightly edited) result of `Array#each`
being inlined into `method_a`. The details don't matter, but there are two
things I want to call out:

* What used to be a dynamic `invokeblock` is now what we call
  `InvokeBlockIseqDirect` because we know from the call context what block
  `method_a` is passing to `Array#each`.
* What used to be a method call is now a loop: the condition check is in
  `bb10`, the body is in `bb9`, and the stuff after the loop is `bb13`/`bb4`.
  If you're interested, try to find the iteration variable and where it gets
  incremented. See the footnote[^iteration-variable] for the answer.

[^iteration-variable]: The iteration variable is `v91`! You can see it get used
    as an array index when it gets unboxed as `v96` and passed to `ArrayAref`.
    You can also see it get incremented with `FixnumAdd` and the constant `1`.
    In `bb10` it has a different name, `v69`, which gets checked against the
    array length.

This is a massive improvement over the previous very generic operations.

```
fn method_a@/tmp/arrayeach.rb:2:
# ...
bb3(v9:BasicObject, v10:BasicObject):
  # ...
  v55:Fixnum[0] = Const Value(0)
  Jump bb10(v25, v55)
bb10(v68:ArrayExact, v69:Fixnum):
  v73:CInt64 = ArrayLength v68
  v74:Fixnum = BoxFixnum v73
  v75:BoolExact = FixnumGe v69, v74
  v77:CBool = Test v75
  CondBranch v77, bb13(), bb9(v68, v69)
bb13():
  CheckInterrupts
  Jump bb4(v68)
bb9(v90:ArrayExact, v91:Fixnum):
  v96:CInt64 = UnboxFixnum v91
  v97:BasicObject = ArrayAref v90, v96
  v99:CPtr = GetEP 0
  v100:CInt64 = LoadField v99, :VM_ENV_DATA_INDEX_SPECVAL@-0x8
  v101:CInt64[-4] = Const CInt64(-4)
  v102:CInt64 = IntAnd v100, v101
  v103:BasicObject = InvokeBlockIseqDirect (0x16f3d5968), v102, v97
  v107:Fixnum[1] = Const Value(1)
  v108:Fixnum = FixnumAdd v91, v107
  PatchPoint NoEPEscape(each)
  Jump bb10(v90, v108)
bb4(v114:BasicObject):
  PopInlineFrame
  PatchPoint NoEPEscape(method_a)
  CheckInterrupts
  Return v114
```

You may notice that there is still one call in the loop: the call to the block
(`InvokeBlockIseqDirect`). That's next on our list to tackle. We are optimistic
that we will soon also be able to inline block calls. Then the whole thing will
*really* be just a loop!

The code that enables us to reason about which block got passed into the
inlined callee (`Array#each`) only landed [a couple of days
ago](https://github.com/ruby/ruby/commit/c35fbd6fb27a13842299597d46b49a10f018c5b0)
(July 10, 2026), written by [Luke Gruber](https://github.com/luke-gruber). This
was one of Luke's first changes to ZJIT.

## So what does performance look like?

Well, for starters, the microbenchmarks that we use as "performance unit tests"
for specific aspects of Ruby went wild. Some benchmarks that were using
block-based looping got *much* faster; they were previously bounded by ZJIT's
block invocation performance.

Take a look at the `cfunc_itself` benchmark, which tests that we can fold away
the call to Ruby's built-in `itself` method. ZJIT ends up optimizing the
`times` method to invoke the `do` block directly, and the `do` block gets
optimized to nothing but a guard on the self's class to make sure it hasn't
changed.

```ruby
run_benchmark(500) do
  500000.times do |i|
    itself
    itself
    itself
    itself
    itself
    itself
    itself
    itself
    itself
    itself
  end
end
```

Because we had previously optimized the body away, the result of the direct
block invocation is a massive speedup:

![The yellow line represents ZJIT speedup over the interpreter. Higher is
better. 35x!](cfunc_itself_benchmark.png){:width="80%"}

Other benchmarks also kind of stop making sense because of the amount of
inlining. Our bmethod benchmark, which benchmarked how fast we can call methods
defined with `define_method`, also stopped measuring anything of use. We're
going to have to rework the benchmark to be more fair...

![The yellow line represents ZJIT speedup over the interpreter. Higher is
better. 139x!](send_bmethod_benchmark.png){:width="80%"}

As expected, larger Rails benchmarks don't see a ton of change; they exercise a
diffuse set of features so optimizing any one feature bumps the big benchmarks
only a little bit.

I am excited to see what happens when we can fully turn `each` and friends into
call-less loops!

## Wrapping up

Code locality rules. The inliner helps inject more of it and reason across
method calls. ZJIT, a little over one year old, is growing up! :')

We're still tuning the inliner knobs. Some of the code in this post required
tweaking `--zjit-inline-threshold` to convince the compiler to inline
`Array#each` into `method_a` because of `Array#each`'s size. It will take some
time for the ZJIT developers to figure out reasonable defaults.

Try out our HIR explorer at [tryzjit.fly.dev](https://tryzjit.fly.dev/). Try
out ZJIT in your application by adding the `--zjit` flag to a Ruby over 4.0.
Thanks for reading and see you next time.

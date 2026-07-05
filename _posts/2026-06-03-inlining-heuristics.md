---
title: "A survey of inlining heuristics"
layout: post
---

Compilers, especially method just-in-time compilers, operate on one function at
a time. It is a natural code unit size, especially for a dynamic language JIT:
at a given point in time, what more information can you gather about other
parts of a running, changing system?

I don't have any data to back this up---maybe I should go gather some---but on
average, methods are small. Especially in languages such as Ruby that use
method dispatch for everything, even instance variable (attribute, field, ...)
lookups, they are *small*. And everywhere.

This makes the compiler sad. If we are to continue to anthropomorphize them,
compilers like having more context so they can optimize better. Consider the
following silly-looking example that is actually representative of a surprising
amount of real-world code:

```ruby
class Point
  attr_reader :x, :y

  def initialize(x, y)
    @x = x
    @y = y
  end

  def distance(other)
    Math.sqrt((@x - other.x)**2 + (@y - other.y)**2)
  end
end

def distance_from_origin(x, y)
  Point.new(x, y).distance(Point.new(0, 0))
end
```

Right now, in the `distance_from_origin` method, I count 8 different method calls:

* `Point.new`
* `Point#initialize`
* `Point.new`
* `Point#initialize`
* `Point#distance`
* `Float#**`
* `Float#**`
* `Math.sqrt`

(Technically more, but the ivar lookups (including `attr_reader`!), addition,
and subtraction are generally specialized and don't push a frame, even in the
interpreter.)

Furthermore, there are at least two heap allocations: one for each `Point`
instance.

Last, there is a bunch of memory traffic to and from `Point` instances.

This all is a huge bummer! What should be a simple math operation is now
overwhelmed with a bunch of other stuff. `Point` is certainly not a zero-cost
abstraction.

Even if we had a bunch of other optimizations such as load-store elimination or
escape analysis, they would not be able to do much: pretty much everything
escapes and is effectful. That is, unless we *inline*. Inlining is the lever
that enables a bunch of other optimization passes to kick in.

## Inlining: the "easy" part

I wrote about the design and implementation of Cinder's inliner ([FB
link](https://engineering.fb.com/2022/05/02/open-source/cinder-jits-instagram/),
[personal blog link](/blog/cinder-jit-inliner/)) a couple of years ago. I wrote
about arguably the simplest part, which is copying the callee body into the
caller. It took me at least a week to get working. Probably closer to months if
you consider all the plumbing through the rest of the JIT. In February during a
small hackathon, I watched my colleague [k0kubun](https://github.com/k0kubun)
prototype that bit of the inliner inside ZJIT in about 30 minutes.

There is more to do when pretty much every part of the VM is observable from
the guest language: both Python and Ruby allow inspecting the state of the
locals, the call stack, etc from user code. Sampling profilers also expect some
amount of breadcrumbs to work with to inspect the stack. So there's some more
machinery still required to pretend like the callee function was not inlined. I
talk about this a little bit in the Cinder blog post.

Even so, all of that can probably be designed and wired together in a couple
of months. Then you will find yourself tuning the inliner for the next 10
years. This is much harder.

## When: the harder part

The thing that makes inlining difficult, especially in a method JIT, is that
you are trying to make an entire (dynamic!) system faster but you are only
looking through a microscope and only capable of local reasoning[^aot-split].
Whereas other optimizations such as strength reduction, inline caches, and
value numbering are an un-alloyed good for the generated code, inlining can
have *negative effects*. It is also perhaps the first optimization people add
that has non-local impact.

[^aot-split]: There are some newer papers, especially in Java land, that try to
    do a lot of analysis ahead-of-time and bundle the resulting information in
    .class files. Then the JIT can read it and see more than local context.

    Or, if you are an AOT compiler, you can probably do a lot more whole system
    reasoning---both for time budget reasons and also because you can see more
    functions at once.

If you inline wrong, your code size might blow up. This might thrash your CPU's
caches. Bummer, but happens to the best of us.

But also, if you inline wrong, you might get in the way of other helpful
optimizations: if you hit some size limit after inlining method A, you might
never get to inline B, which is the key to unlocking the performance of the
method you are trying to optimize.

Last, inlining might hurt compile time. In situations where latency is
paramount (think: interactive client JavaScript), adding tons more code into
the fray might add noticeable hiccups, even if the long-term throughput
improves. As always, in-band compilation is a trade-off because any time you
spend compiling, you are *not executing code*.

You have to write your compiler to reason about all of this stuff. So you have
heuristics. For example, here is Michael Pollan's inliner heuristic:

> *Inline methods. Mostly small. Not too many.*

I did a survey of a bunch of compilers, mostly JIT compilers, to see what their
inlining heuristics look like. I also read (skimmed) some papers to see what
those folks had to say. I wonder if they agree.

This post was a long time coming. I started working on it about five years ago
but then when I quit working at Facebook I accidentally left behind all of the
inliner research I did for Cinder's inliner. So then I kind of just thought
about it aimlessly for a while before redoing it this year. Anyway, here's
wonderwall.

## The heuristics

Spoiler alert: all in all, people tend to look at:

* Profiles of call target
* Cumulative caller size (increasing as callees get inlined)
* Callee size
* Inline depth
* Number of inlined calls at a certain depth
* If recursion is present
* Callee/caller call count ratio (if callee only called less than K% of calls
  to caller, don't inline callee)
* Callee stack usage
* Polymorphism in callee
* What mode the compiler is in (baseline vs more aggressive)
* If the callee looks like it always raises/throws

And also have different interesting ways to pipe in profile information.

Last, some newer papers do some wild stuff:

* Train neural networks to make inlining decisions
* Let inlining drive the entire optimization pipeline, treating it as a search
  heuristic over a BFS walk of the call graph
* Use AOT-gathered information to aid in JIT heuristics

Another thing to consider in inlining is how you gather and interpret profiles.

## Call context and profiles: the other harder part

When you compile a function, you tend to specialize it based on the input it
has historically been given. For a monomorphic input, maybe you guard that the
type is still the same and otherwise jump into the interpreter. For a
polymorphic input, maybe you check the top K (~4) common cases and otherwise
jump into the interpreter. Fine.

But sometimes you can be compiling a polymorphic method `bar` that is actually
monomorphic in its caller `foo`. That is, `foo` might only ever pass one kind
of input to `bar`, but other callers pass all kinds of stuff. Here is a bit of
a silly example to show what I mean:

```ruby
class HashWithIndifferentAccess
  def initialize
    @hash = {}
  end

  # Allow reading from the Hash with either a String or a Symbol
  def [](key) = @hash[key.to_sym]

  # ...
end

# some method...
some_hash = HashWithIndifferentAccess.new
# ...
some_hash["abc"]

# some other method...
another_hash = HashWithIndifferentAccess.new
# ...
another_hash[:xyz]
```

Just kidding, not so silly at all. It's a super common pattern [in
Rails][hwia]. It makes `key` polymorphic in `HashWithIndifferentAccess#[]` even
though for many of its callers, it may well be monomorphic (or even a
constant).

[hwia]: https://github.com/rails/rails/blob/6c75e6d5663afa4278ee593c2d6c20c1ee396e32/activesupport/lib/active_support/hash_with_indifferent_access.rb#L55

In order to plumb this information through to the compiler, you have to figure
out this call context relationship. There are a couple of common ways to do it.

### Splitting

YJIT, for example, though it does not inline, splits methods based on the types
of the arguments going in. This means that it clones the compiled code,
generating a new version for each context. This does not give *call* context
("A calls B") but gives type context ("B is called with integers, B' is called
with strings").

A compiler could do type-based splitting in the interpreter or a baseline tier.

### Profile splitting

If you don't fancy duplicating the code, you can instead duplicate the
profiles. You could either do this using type context (as above) or using call
context. SpiderMonkey, for example, does "trial inlining" that allows callers
to pass down a bit of memory for potential inline candidate callees to record
their inline caches. Instead of each function holding its own ICScript, the
caller allocates a unique ICScript for that potential-inline call-site. This
gives each callee function (at least?) one level of call context.

Later, when inlining the callee into the caller, we don't have other callers'
type information polluting the IR builder (or whatever reads the profiles).

### Bytecode inlining

~~JavaScriptCore handles this by inlining bytecode into other bytecode. This is a
gnarly transformation but gives the interpreter, even (!) access to call
context. On tier-up to the compiler, all the inlining decisions have been made
already.~~

EDIT: Upon reflection, I think think this is wrong and based on a misremembered
conversation.

### Early tier with counters

HotSpot handles this with multiple tiers. The interpreter tiers up to the
client compiler, C1. C1 profiles branch and call targets in compiled code. C1
may eventually recompile based on this new information. C1 may eventually tier
up to C2, which copies C1 inlining decisions. This way, we get call context in
profiles via inlining.

### Inline and analyze and hope

One last thing you could do is just trust your type inference and branch
folding in the optimizer. You could inline and do polymorphic specialization in
the callee when building the IR, then hope that your branch pruning
monomorphizes the inlined callee. It's a little wasteful because the
polymorphic code is built "for nothing", but it might work fine?

<!--
### Inline and merge profiles
-->

Okay, onto the collected notes and half-baked commentary. Here's a survey of a
bunch of JIT compilers and how they reason about inlining heuristics.

### Thanks

But before we get into that, thanks to Iain Ireland, CF Bolz-Tereick, and Ian
Rogers for feedback on this blog post!

## The survey: bits and bobbles

What follows is mostly a "bits and bobbles" section a la [Phil
Zucker](https://www.philipzucker.com/).

We'll start with [Cinder][cinder], because when I wrote Cinder's inliner I
added only the simplest heuristics, mostly "don't inline" signals. Over time,
after I left, people tuned it a bit more.

[cinder]: https://github.com/facebookincubator/cinderx


### Cinder

The [inliner][cinder-inliner] starts from the caller CFG, walking it to find
suitable inlining candidates. Inlining candidates are only for call targets
that are known---in Cinder's case, only for monomorphic call targets---and pass
some checks. The callee is only known by it's function object, which includes
its bytecode. There is no IR available for the callee until we decide to inline.

[cinder-inliner]: https://github.com/facebookincubator/cinderx/blob/88189ebf4bfd196ac7578c5076efa39bfa11f211/cinderx/Jit/hir/inliner.cpp#L341

Most of the "can't handle this" checks are related to argument handling. Python
has a pretty complex calling convention, so if the caller/callee have not
agreed on how the arguments should be passed through, the inliner doesn't care
to try and figure it out on its own. That is the responsibility of [other parts
of the compiler][cinder-resolve-args]. Things in this `canInline`
function could be considered "TODO".

[cinder-resolve-args]: https://github.com/facebookincubator/cinderx/blob/88189ebf4bfd196ac7578c5076efa39bfa11f211/cinderx/Jit/hir/simplify.cpp#L1765

```c++
bool canInline(Function& caller, AbstractCall* call_instr) {
  // ...
  BorrowedRef<PyFunctionObject> func = call_instr->func;
  auto fail = [&](InlineFailureType failure_type) {
    dlogAndCollectFailureStats(caller, call_instr, failure_type);
    return false;
  };

  if (func->func_kwdefaults != nullptr) {
    return fail(InlineFailureType::kHasKwdefaults);
  }

  BorrowedRef<PyCodeObject> code{func->func_code};
  JIT_CHECK(PyCode_Check(code), "Expected PyCodeObject");

  if (code->co_kwonlyargcount > 0) {
    return fail(InlineFailureType::kHasKwOnlyArgs);
  }
  // ...
}
```

Failures are logged so they can be analyzed. If the Cinder team determines that
there is some very frequent case they should handle, they will find out from
the logs.

The inliner collects all candidate call instructions in one pass over the CFG.
It loads the configurable "cost limit" from the options struct. Then it does
one pass over the inlining candidates vector, inlining until it (maybe) hits
the cost limit.

```c++
// ...
size_t cost_limit = getConfig().inliner_cost_limit;
size_t cost = codeCost(irfunc.code);

// Inline as many calls as possible, starting from the top of the function and
// working down.
for (auto& call : to_inline) {
  BorrowedRef<PyCodeObject> call_code{call.func->func_code};
  size_t new_cost = cost + codeCost(call_code);
  if (new_cost > cost_limit) {
    LOG_INLINER(
        "Inliner reached cost limit of {} when trying to inline {} into {}, "
        "inlining stopping early",
        new_cost,
        funcFullname(call.func),
        irfunc.fullname);
    break;
  }
  cost = new_cost;

  inlineFunctionCall(irfunc, &call);

  // We need to reflow types after every inline to propagate new type
  // information from the callee.
  reflowTypes(irfunc);
}
// ...
```

It does some graph maintenance work after inlining these calls, but that's it.

This approach gets a surprising amount of utility for being so simple: it
inlines constants (quite a few methods look like `def foo(): return 5`), small
methods, and (at least, as far as I can remember) shrinks the compiled code
size. All for very little compile time overhead.

There's one other "standalone" Python JIT out there, PyPy. So we should look at
that too.

### PyPy

There are two inliners in PyPy. One is inside the RPython to C translation
pipeline, which acts more like an ahead-of-time compiler[^rpython-inliner].
Then there is the tracing JIT bit, which has its own optimizer and heuristics.
We're going to look at the latter.

[^rpython-inliner]: [Check it
    out](https://github.com/pypy/pypy/blob/bab69dca82606f9e4feaf5507f8dd8dfb3e968b2/rpython/translator/backendopt/inline.py#L144)
    if you like. I stumbled across it by accident.

I talked to [CF Bolz-Tereick](https://cfbolz.de/) about the inliner and their
comment was that PyPy's inlining heuristic is "yes". There are a couple of
exceptions, such as not inlining recursive functions or functions with loops.
But the basic idea of tracing includes tracing through call instructions, which
naturally means that you are "inlining".

PyPy also does this neat thing where they treat frame pushes like normal
allocation. Frame pushes, frame reads, and frame writes get written to the
trace like normal object memory traffic and can get optimized away like other
field reads and writes. This means that they can "just" use DCE to eliminate
frame pushes and pops, whereas Cinder has some complicated mechanism to do it
(which is my fault).

TODO get more details here

### V8

V8 is a JS engine and it has over the years had many execution approaches.
We'll look at three of them since they all have or had their place in the
history:

* Hydrogen was the first real SSA IR and it looks very familiar to me, having
  worked on Cinder and now ZJIT. It is now defunct.
* Turbofan was the replacement, going full Sea of Nodes. In the grand scheme of
  things it is a pretty fast compiler, but it does not hold back from doing some
  expensive rewrites. This was recently rewritten from Sea of Nodes to a mode
  traditional CFG and nicknamed Turboshaft.
* Maglev is meant to coexist alongside Turbofan, preferring to speculate a little
  more eagerly and do fewer incremental rewrites in the name of compile
  time.[^turbolev]

[^turbolev]: See also "Turbolev", which seems to merge Maglev (CFG) with
    Turbofan (Sea of Nodes)... somehow.

They also each inline at different times in the pipeline, which made for a fun
time trying to understand the different codebases.

#### V8 Hydrogen

Inlining happens during Hydrogen graph building
* <https://github.com/tekknolagi/v8/blob/a969ab67f8e1e7475d9b26468225c3a772890c64/src/crankshaft/hydrogen.cc#L9236>

Don't store function bytecode of all functions; need to re-parse callee *text
source* to inline

Heuristics <https://github.com/tekknolagi/v8/blob/a969ab67f8e1e7475d9b26468225c3a772890c64/src/crankshaft/hydrogen.cc#L7807>
* something about native context
* check callee AST size against configurable limit
* check inlining depth against configurable limit
* don't inline recursive functions
* check current cumulative method size (as tracked by AST node count) against
  configurable limit

#### V8 TurboFan

<https://docs.google.com/document/d/1VoYBhpDhJC4VlqMXCKvae-8IGuheBGxy32EOgC2LnT8/edit>

<https://github.com/v8/v8/blob/036842f4841326130a40adfcff38f85a9b4cd30a/src/compiler/js-inlining-heuristic.h#L14>

* Find candidates <https://github.com/v8/v8/blob/036842f4841326130a40adfcff38f85a9b4cd30a/src/compiler/js-inlining-heuristic.cc#L134>
* Can inline <https://github.com/v8/v8/blob/036842f4841326130a40adfcff38f85a9b4cd30a/src/compiler/js-inlining-heuristic.cc#L75>
* Force inline small functions <https://github.com/v8/v8/blob/036842f4841326130a40adfcff38f85a9b4cd30a/src/compiler/js-inlining-heuristic.cc#L309>
* Loop over sorted (by comparator) list <https://github.com/v8/v8/blob/036842f4841326130a40adfcff38f85a9b4cd30a/src/compiler/js-inlining-heuristic.cc#L847>

#### V8 Maglev

When optimizing, add call instructions to the inline candidates list: <https://github.com/v8/v8/blob/1a391f98cc7a9196369f2d6cab7df35ffbe92c08/src/maglev/maglev-graph-optimizer.cc#L1271>

```c++
ProcessResult MaglevGraphOptimizer::VisitCall(Call* node,
                                              const ProcessingState& state) {
  // ...
  int bytecode_length = shared.GetBytecodeArray(broker()).length();
  float score =
      (call_frequency / bytecode_length) * (loop_depth_ > 0 ? 1.5 : 1.0);

  bool is_small_function =
      bytecode_length <
      reducer_.graph()->compilation_info()->flags().max_eager_inlined_bytecode;
  // ...
  MaglevCallSiteInfo* call_site = reducer_.zone()->New<MaglevCallSiteInfo>(
      MaglevCallerDetails{
          ...
          is_small_function, call_frequency,
          ...
      },
      score, bytecode_length);
  reducer_.PushInlineCandidate(call_site);
  // ...
}
```

<https://github.com/v8/v8/blob/036842f4841326130a40adfcff38f85a9b4cd30a/src/maglev/maglev-inlining.h#L36>

Unlike for example Cinder, Maglev looks like it does not have a lot of
restrictions about what can get inlined into what, so its "can inline" signal
is about budget. Actually two budgets: small budget and normal budget.

```c++
bool MaglevInliner::CanInlineCall() {
  // We stop inlining entirely if the small budget is exhausted.
  // Inlining decisions after that become bad if we stop inlining small
  // functions, but keep inlining large ones.
  return !graph_->inlineable_calls().empty() &&
         (graph_->total_inlined_bytecode_size() <
              max_inlined_bytecode_size_cumulative() ||
          graph_->total_inlined_bytecode_size_small() <
              max_inlined_bytecode_size_small_total());
}
```

Then its inlining loop is a greedy walk of the to-inline queue checking
candidate sizes.

```c++
bool MaglevInliner::InlineCallSites() {
  DCHECK(CanInlineCall());
  while (!graph_->inlineable_calls().empty()) {
    // pop from inlineable_calls
    MaglevCallSiteInfo* call_site = ChooseNextCallSite();

    bool is_small_with_heapnum_input_outputs =
        IsSmallWithHeapNumberInputsOutputs(call_site);

    if (graph_->total_inlined_bytecode_size() >
        max_inlined_bytecode_size_cumulative()) {
      // We ran out of budget. Checking if this is a small-ish function that we
      // can still inline.
      if (graph_->total_inlined_bytecode_size_small() >
          max_inlined_bytecode_size_small_total()) {
        graph_->compilation_info()->set_could_not_inline_all_candidates();
        break;
      }

      if (!is_small_with_heapnum_input_outputs) {
        graph_->compilation_info()->set_could_not_inline_all_candidates();
        // Not that we don't break just rather just continue: next candidates
        // might be inlineable.
        continue;
      }
    }

    InliningResult result =
        BuildInlineFunction(call_site, is_small_with_heapnum_input_outputs);
    // ...
  }
  return true;
}
```

It runs this loop (which drains the queue) interleaved with the optimizer
(which populates the queue).

```c++
bool MaglevInliner::Run() {
  if (graph_->inlineable_calls().empty()) return true;

  while (CanInlineCall()) {
    if (!InlineCallSites()) return false;
    RunOptimizer();
  }
  // ...
}
```

Confusingly, though, the optimizer also calls another function called
`CanInlineCall` which checks if it legally can inline:
* skip recursion
* <https://github.com/v8/v8/blob/1a391f98cc7a9196369f2d6cab7df35ffbe92c08/src/objects/shared-function-info-inl.h#L421>
* not called enough (min call frequency)
* bytecode too big

`bool MaglevGraphBuilder::ShouldEagerInlineCall(` ~~appears unused? / dead
declaration?~~ maybe src/maglev/maglev-graph-builder.cc is just not working on
github search

`MaybeReduceResult MaglevGraphBuilder::TryBuildCallKnownJSFunction(` ~~also
unused / dead declaration~~ same

### JavaScriptCore

JavaScriptCore is funky! Unlike these other compilers that do inlining in their
neat little SSA IRs, JSC inlines *at the bytecode level*[^fil]. This is their way of
making sure that they get at least one level of call context into their
interpreter inline caches, which will eventually give better information to the
compiler.

[^fil]: Potentially a misunderstanding based on a private conversation. I'm
    working on tracking down the implementation...

* Bytecode inlining
  * <https://github.com/WebKit/WebKit/blob/709c3895afd71e0836f8c8be7393e44d41fab7e1/Source/JavaScriptCore/bytecode/CodeBlock.cpp#L2453>
* DFG
  * <https://github.com/WebKit/WebKit/blob/709c3895afd71e0836f8c8be7393e44d41fab7e1/Source/JavaScriptCore/dfg/DFGCapabilities.cpp#L76>
  * <https://github.com/WebKit/WebKit/blob/917854a9c245b87b333e23ed4b195505d574a333/Source/JavaScriptCore/dfg/DFGByteCodeParser.cpp#L1703>
  * <https://github.com/WebKit/WebKit/blob/917854a9c245b87b333e23ed4b195505d574a333/Source/JavaScriptCore/bytecode/CallLinkStatus.cpp#L294>
  * <https://github.com/WebKit/WebKit/blob/d919344236c47b610930636d3310f00380624d43/Source/JavaScriptCore/bytecode/InlineCallFrame.h>

JSC only inlines based on bytecode profile information, and only inlines
bytecode??

TODO find better sources for bytecode inlining

<!--
Compile plan
https://github.com/WebKit/WebKit/blob/709c3895afd71e0836f8c8be7393e44d41fab7e1/Source/JavaScriptCore/dfg/DFGPlan.cpp#L186
-->

### SpiderMonkey

SpiderMonkey has another way of getting that call context without doing bytecode
inlining: they add call context to their inline caches. Methods can pass down
an *ICScript* to their callees where the callee writes its inline cache
information. Then, when compiling, the callee is more likely to be
monomorphized.

Wasm

<https://github.com/mozilla-firefox/firefox/blob/438a3ce10eb77fb50d968463b7741117aec5bb4a/js/src/wasm/WasmHeuristics.h#L213>

SpiderMonkey ICScript

### Wasmtime and Cranelift

<https://fitzgen.com/2025/11/19/inliner.html>

### HotSpot

Plan: run in interpreter; tier up to C1; profile call targets; inline in C1;
profile branch counts; tier up to C2, which copies C1 inlining decisions in
bytecode parser

HotSpot C2

<https://github.com/openjdk/jdk/blob/a05d5d2514c835f2bfeaf7a8c7df0ac241f0177f/src/hotspot/share/opto/bytecodeInfo.cpp#L116>

<https://github.com/openjdk/jdk/blob/497dca2549a9829530670576115bf4b8fab386b3/src/hotspot/share/opto/bytecodeInfo.cpp#L197>

<https://github.com/openjdk/jdk/blob/497dca2549a9829530670576115bf4b8fab386b3/src/hotspot/share/opto/parse.hpp#L42>

<https://github.com/openjdk/jdk/blob/497dca2549a9829530670576115bf4b8fab386b3/src/hotspot/share/opto/doCall.cpp#L185>

Not too small

Walk up the call stack to figure out what to compile

Handling the right thing to inline: def foo(a) = a.each {|x| x }
want to compile `foo`, inline each, inline block, not compile block separately
(probably)

HotSpot C1

<https://bernsteinbear.com/assets/img/design-hotspot-client-compiler.pdf>

<https://github.com/openjdk/jdk/blob/d854a04231a437a6af36ae65780961f40f336343/src/hotspot/share/c1/c1_GraphBuilder.cpp#L755>

<https://github.com/openjdk/jdk/blob/d854a04231a437a6af36ae65780961f40f336343/src/hotspot/share/c1/c1_GraphBuilder.cpp#L3854>

* skip callees with exception handlers (unless explicitly allowed with a CLI flag)
* skip synchronized callees (unless explicitly allowed with a CLI flag)
* skip classes with unlinked callees
* skip uninitialized classes
* ...

heuristics:
* max inline level (default 9)
* max recursive inline level (default 1)
* callee bytecode size (max for top level is 35 bytecodes, but falls off by 10% per inline level)
* callee stack usage (max of 10 slots)
    ```c++
        // Additional condition to limit stack usage for non-recursive calls.
        if ((callee_recursive_level == 0) &&
            (callee->max_stack() + callee->max_locals() - callee->size_of_parameters() > C1InlineStackLimit)) {
          INLINE_BAILOUT("callee uses too much stack");
        }
    ```
* max total method size (default 8000 bytecodes)

### TruffleRuby

TruffleRuby uses weighted compile queue

Graal
<https://ieeexplore.ieee.org/document/8661171>

### .NET

<https://github.com/dotnet/runtime/blob/2d638dc1179164a08d9387cbe6354fe2b7e4d823/docs/design/coreclr/jit/inlining-plans.md>

<https://github.com/dotnet/runtime/blob/0b3f3ab1ecf4de06459e5f0e2b7cb3baf70ef981/src/coreclr/jit/inline.def#L94>

<https://github.com/dotnet/runtime/blob/0b3f3ab1ecf4de06459e5f0e2b7cb3baf70ef981/src/coreclr/jit/inlinepolicy.cpp>

<https://github.com/dotnet/runtime/blob/0b3f3ab1ecf4de06459e5f0e2b7cb3baf70ef981/docs/design/coreclr/jit/inline-size-estimates.md?plain=1#L5>
<https://github.com/dotnet/runtime/blob/0b3f3ab1ecf4de06459e5f0e2b7cb3baf70ef981/src/coreclr/jit/fginline.cpp>

<https://github.com/dotnet/runtime/issues/10303>

<https://github.com/AndyAyersMS/PerformanceExplorer/blob/master/notes/notes-aug-2016.md>
<!--
LSRA heuristics
https://github.com/dotnet/runtime/blob/2d638dc1179164a08d9387cbe6354fe2b7e4d823/docs/design/coreclr/jit/lsra-heuristic-tuning.md
-->

### Dart

<https://github.com/dart-lang/sdk/blob/391212f3da8cc0790fc532d367549042216bd5ca/runtime/vm/compiler/backend/inliner.cc#L49>

<https://github.com/dart-lang/sdk/blob/391212f3da8cc0790fc532d367549042216bd5ca/runtime/vm/compiler/backend/inliner.cc#L1023>

<https://web.archive.org/web/20170830093403id_/https://link.springer.com/content/pdf/10.1007/978-3-540-78791-4_5.pdf>

```c++
DEFINE_FLAG(int,
            deoptimization_counter_inlining_threshold,
            12,
            "How many times we allow deoptimization before we stop inlining.");
DEFINE_FLAG(bool, trace_inlining, false, "Trace inlining");
DEFINE_FLAG(charp, inlining_filter, nullptr, "Inline only in named function");

// Flags for inlining heuristics.
DEFINE_FLAG(int,
            inline_getters_setters_smaller_than,
            10,
            "Always inline getters and setters that have fewer instructions");
DEFINE_FLAG(int,
            inlining_depth_threshold,
            6,
            "Inline function calls up to threshold nesting depth");
DEFINE_FLAG(
    int,
    inlining_size_threshold,
    25,
    "Always inline functions that have threshold or fewer instructions");
DEFINE_FLAG(int,
            inlining_callee_call_sites_threshold,
            1,
            "Always inline functions containing threshold or fewer calls.");
DEFINE_FLAG(int,
            inlining_callee_size_threshold,
            160,
            "Do not inline callees larger than threshold");
DEFINE_FLAG(int,
            inlining_small_leaf_size_threshold,
            50,
            "Do not inline leaf callees larger than threshold");
DEFINE_FLAG(int,
            inlining_caller_size_threshold,
            50000,
            "Stop inlining once caller reaches the threshold.");
DEFINE_FLAG(int,
            inlining_hotness,
            10,
            "Inline only hotter calls, in percents (0 .. 100); "
            "default 10%: calls above-equal 10% of max-count are inlined.");
DEFINE_FLAG(int,
            inlining_recursion_depth_threshold,
            1,
            "Inline recursive function calls up to threshold recursion depth.");
DEFINE_FLAG(int,
            max_inlined_per_depth,
            500,
            "Max. number of inlined calls per depth");
```

[An adaptive strategy for inline substitution](/assets/img/adaptive-inline.pdf) (PDF)

```c++
  // Inlining heuristics based on Cooper et al. 2008.
  InliningDecision ShouldWeInline(const Function& callee,
                                  intptr_t instr_count,
                                  intptr_t call_site_count) {
    // Pragma or size heuristics.
    if (inliner_->AlwaysInline(callee)) {
      return InliningDecision::Yes("AlwaysInline");
    } else if (inlined_size_ > FLAG_inlining_caller_size_threshold) {
      // Prevent caller methods becoming humongous and thus slow to compile.
      return InliningDecision::No("--inlining-caller-size-threshold");
    } else if (instr_count > FLAG_inlining_callee_size_threshold) {
      // Prevent inlining of callee methods that exceed certain size.
      return InliningDecision::No("--inlining-callee-size-threshold");
    }
    // Inlining depth.
    const int callee_inlining_depth = callee.inlining_depth();
    if (callee_inlining_depth > 0 &&
        ((callee_inlining_depth + inlining_depth_) >
         FLAG_inlining_depth_threshold)) {
      return InliningDecision::No("--inlining-depth-threshold");
    }
    // Situation instr_count == 0 denotes no counts have been computed yet.
    // In that case, we say ok to the early heuristic and come back with the
    // late heuristic.
    if (instr_count == 0) {
      return InliningDecision::Yes("need to count first");
    } else if (instr_count <= FLAG_inlining_size_threshold) {
      return InliningDecision::Yes("--inlining-size-threshold");
    } else if (call_site_count <= FLAG_inlining_callee_call_sites_threshold) {
      return InliningDecision::Yes("--inlining-callee-call-sites-threshold");
    }
    return InliningDecision::No("default");
  }
```

<!--
CompileType
https://github.com/dart-lang/sdk/blob/d3c0a3768bd4be4a92886e136811b5f748b63ddd/runtime/vm/compiler/backend/compile_type.h#L43
-->

<!--
intrinsics
https://github.com/dart-lang/sdk/blob/d3c0a3768bd4be4a92886e136811b5f748b63ddd/runtime/vm/compiler/call_specializer.cc#L3229
-->

### HHVM

tracelet based

<https://github.com/facebook/hhvm/blob/eeba7ad1ffa372a9b8cc9d1ec7f5295d45627009/hphp/runtime/vm/jit/inlining-decider.h#L89>

```c++
  // Refuse if the cost exceeds our thresholds.
  // We measure the cost of inlining each callstack and stop when it exceeds a
  // certain threshold.  (Note that we do not measure the total cost of all the
  // inlined calls for a given caller---just the cost of each nested stack.)
  cost = costOfInlining(callerSk, callee, regionAndUnit, annotationsPtr);
  if (cost <= Cfg::HHIR::AlwaysInlineVasmCostLimit) {
    return accept(folly::sformat("cost={} within always-inline limit", cost));
  }

  if (region.instrSize() > irgs.budgetBCInstrs) {
    return refuse(folly::sformat(
      "exhausted bytecode budget: budgetBCInstrs={}, regionSize={}",
      irgs.budgetBCInstrs, region.instrSize()));
  }
  auto maxTotalCost = adjustedMaxVasmCost(irgs, region, inlineDepth(irgs));
  int maxCost = maxTotalCost;
  if (Cfg::HHIR::InliningUseStackedCost) {
    maxCost -= irgs.inlineState.cost;
  }
  const auto baseProfCount = s_baseProfCount.load();
  const auto callerProfCount = irgen::curProfCount(irgs);
  const auto calleeProfCount = irgen::calleeProfCount(irgs, region);
  if (cost > maxCost) {
    auto const depth = inlineDepth(irgs);
    return refuse(folly::sformat(
      "too expensive: cost={} : maxCost={} : "
      "baseProfCount={} : callerProfCount={} : calleeProfCount={} : depth={}",
      cost, maxCost, baseProfCount, callerProfCount, calleeProfCount, depth));
  }

  return accept(folly::sformat("small region with return: cost={} : "
                               "maxTotalCost={} : maxCost={} : baseProfCount={}"
                               " : callerProfCount={} : calleeProfCount={}",
                               cost, maxTotalCost, maxCost, baseProfCount,
                               callerProfCount, calleeProfCount));
```

### ART

<https://github.com/LineageOS/android_art/blob/8ce603e0c68899bdfbc9cd4c50dcc65bbf777982/compiler/optimizing/inliner.h>

```c++
// Instruction limit to control memory.
static constexpr size_t kMaximumNumberOfTotalInstructions = 1024;

// Maximum number of instructions for considering a method small,
// which we will always try to inline if the other non-instruction limits
// are not reached.
static constexpr size_t kMaximumNumberOfInstructionsForSmallMethod = 3;

// Limit the number of dex registers that we accumulate while inlining
// to avoid creating large amount of nested environments.
static constexpr size_t kMaximumNumberOfCumulatedDexRegisters = 32;

// Limit recursive call inlining, which do not benefit from too
// much inlining compared to code locality.
static constexpr size_t kMaximumNumberOfRecursiveCalls = 4;

// Limit recursive polymorphic call inlining to prevent code bloat, since it can quickly get out of
// hand in the presence of multiple Wrapper classes. We set this to 0 to disallow polymorphic
// recursive calls at all.
static constexpr size_t kMaximumNumberOfPolymorphicRecursiveCalls = 0;

// Controls the use of inline caches in AOT mode.
static constexpr bool kUseAOTInlineCaches = true;

// Controls the use of inlining try catches.
static constexpr bool kInlineTryCatches = true;
```

```c++
void HInliner::UpdateInliningBudget() {
  if (total_number_of_instructions_ >= kMaximumNumberOfTotalInstructions) {
    // Always try to inline small methods.
    inlining_budget_ = kMaximumNumberOfInstructionsForSmallMethod;
  } else {
    inlining_budget_ = std::max(
        kMaximumNumberOfInstructionsForSmallMethod,
        kMaximumNumberOfTotalInstructions - total_number_of_instructions_);
  }
}
```

```c++
bool HInliner::IsInliningEncouraged(const HInvoke* invoke_instruction,
                                    ArtMethod* method,
                                    const CodeItemDataAccessor& accessor) const {
  if (CountRecursiveCallsOf(method) > kMaximumNumberOfRecursiveCalls) {
    LOG_FAIL(stats_, MethodCompilationStat::kNotInlinedRecursiveBudget)
        << "Method "
        << method->PrettyMethod()
        << " is not inlined because it has reached its recursive call budget.";
    return false;
  }

  size_t inline_max_code_units = codegen_->GetCompilerOptions().GetInlineMaxCodeUnits();
  if (accessor.InsnsSizeInCodeUnits() > inline_max_code_units) {
    LOG_FAIL(stats_, MethodCompilationStat::kNotInlinedCodeItem)
        << "Method " << method->PrettyMethod()
        << " is not inlined because its code item is too big: "
        << accessor.InsnsSizeInCodeUnits()
        << " > "
        << inline_max_code_units;
    return false;
  }

  if (graph_->IsCompilingBaseline() &&
      accessor.InsnsSizeInCodeUnits() > CompilerOptions::kBaselineInlineMaxCodeUnits) {
    LOG_FAIL_NO_STAT() << "Reached baseline maximum code unit for inlining  "
                       << method->PrettyMethod();
    outermost_graph_->SetUsefulOptimizing();
    return false;
  }

  if (invoke_instruction->GetBlock()->GetLastInstruction()->IsThrow()) {
    LOG_FAIL(stats_, MethodCompilationStat::kNotInlinedEndsWithThrow)
        << "Method " << method->PrettyMethod()
        << " is not inlined because its block ends with a throw";
    return false;
  }

  return true;
}
```

```c++
if (outermost_graph_->IsCompilingBaseline() &&
    (current->IsInvokeVirtual() || current->IsInvokeInterface()) &&
    ProfilingInfoBuilder::IsInlineCacheUseful(current->AsInvoke(), codegen_)) {
  uint32_t maximum_inlining_depth_for_baseline =
      InlineCache::MaxDexPcEncodingDepth(
          outermost_graph_->GetArtMethod(),
          codegen_->GetCompilerOptions().GetInlineMaxCodeUnits());
  if (depth_ + 1 > maximum_inlining_depth_for_baseline) {
    LOG_FAIL_NO_STAT() << "Reached maximum depth for inlining in baseline compilation: "
                       << depth_ << " for " << callee_graph->GetArtMethod()->PrettyMethod();
    outermost_graph_->SetUsefulOptimizing();
    return false;
  }
}
```

### JikesRVM

<https://github.com/JikesRVM/JikesRVM/blob/5072f19761115d987b6ee162f49a03522d36c697/rvm/src/org/jikesrvm/compilers/opt/inlining/DefaultInlineOracle.java#L55>

### Other/research

Partial inlining

[Understanding and Exploiting Optimal Function Inlining](https://ethz.ch/content/dam/ethz/special-interest/infk/ast-dam/documents/Theodoridis-ASPLOS22-Inlining-Paper.pdf) (PDF)

machine learning

[Automatic construction of inlining heuristics using machine learning](https://ieeexplore.ieee.org/document/6495004)

[Machine-Learning-Based Optimization Heuristics in Dynamic Compilers](https://ssw.jku.at/Teaching/PhDTheses/Mosaner/Dissertation%20Mosaner.pdf) (PDF)

[Guiding Inlining Decisions Using Post-Inlining Transformations](https://webdocs.cs.ualberta.ca/~amaral/thesis/ErickOchoaMSc.pdf) (PDF)

[U Can’t Inline This!](https://karimali.ca/resources/papers/ourinliner.pdf) (PDF)

[Towards better inlining decisions using inlining trials](https://dl.acm.org/doi/10.1145/182409.182489)

[RhizomeRuby inlining](https://github.com/chrisseaton/rhizome/blob/main/doc/inlining.md)

[An Optimization-Driven Incremental Inline Substitution Algorithm for Just-in-Time Compilers](http://aleksandar-prokopec.com/resources/docs/prio-inliner-final.pdf) (PDF)

[Automatic Tuning of Inlining Heuristics](https://www.cresco.enea.it/SC05/schedule/pdf/pap274.pdf) (PDF)

[Inlining-Benefit Prediction with Interprocedural Partial Escape Analysis](https://dl.acm.org/doi/pdf/10.1145/3563838.3567677) (PDF)

[Inlining of Virtual Methods](/assets/img/virtual-inlining.pdf) (PDF)

[A Study of Type Analysis for Speculative Method Inlining in a JIT Environment](/assets/img/sable-inlining.pdf) (PDF)

[A Comparative Study of Static and Profile-Based Heuristics for Inlining](https://dl.acm.org/doi/epdf/10.1145/351403.351416) (PDF)

clusters from [Custom benefit-driven inliner in Falcon JIT](https://llvm.org/devmtg/2022-05/slides/2022EuroLLVM-CustomBenefitDrivenInliner-in-FalconJIT.pdf) (PDF)


### Graal

<https://github.com/oracle/graal/blob/5dde777cba22a99ebe3f19745d03ddfbc35c563c/compiler/src/jdk.graal.compiler/src/jdk/graal/compiler/phases/common/inlining/policy/GreedyInliningPolicy.java>

<https://github.com/oracle/graal/blob/5dde777cba22a99ebe3f19745d03ddfbc35c563c/compiler/src/jdk.graal.compiler/src/jdk/graal/compiler/phases/common/inlining/InliningPhase.java>

<https://github.com/oracle/graal/blob/5dde777cba22a99ebe3f19745d03ddfbc35c563c/compiler/src/jdk.graal.compiler/src/jdk/graal/compiler/phases/common/inlining/info/elem/InlineableGraph.java#L148>
<!--
GVN
https://github.com/oracle/graal/blob/5dde777cba22a99ebe3f19745d03ddfbc35c563c/compiler/src/jdk.graal.compiler/src/jdk/graal/compiler/phases/common/DominatorBasedGlobalValueNumberingPhase.java#L132
-->

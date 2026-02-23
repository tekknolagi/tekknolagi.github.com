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
following silly-looking example that is actually representative of a lot of
real-world code:

```ruby
class Point
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

(Technically more, but the ivar lookups, addition, and subtraction, are
generally specialized and don't push a frame, even in the interpreter.)

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
caller. It took me at least a week to get working. The other week, I watched my
colleague [k0kubun](https://github.com/k0kubun) replicate that bit of the
inliner inside ZJIT in about 30 minutes.

There is more to do when pretty much every part of the VM is observable from
the guest language: both Python and Ruby allow inspecting the state of the
locals, the call stack, etc from user code. Sampling profilers also expect some
amount of breadcrumbs to work with to inspect the stack. So there's some more machinery
still required to pretend like the function was not inlined. I talk about this
a little bit in the Cinder blog post.

Even so, all of that can probably be designed and wired together in a couple
of months. Then you will find yourself tuning the inliner for the next 10
years. This is much harder.

## When: the harder part

The thing that makes inlining difficult, especially in a method JIT, is that
you are trying to make an entire (dynamic!) system faster but you are only
looking through a microscope and only capable of local reasoning. Whereas other
optimizations such as strength reduction, inline caches, and value numbering
are an un-alloyed good for the generated code, inlining can have *negative
effects*. It is perhaps the first optimization that has non-local impact.

If you inline wrong, your code size might blow up. This might thrash your CPU's
caches. Bummer, but happens to the best of us.

But also, if you inline wrong, you might get in the way of other helpful
optimizations: if you hit some size limit after inlining method A, you might
never get to inline B, which is the key to unlocking the performance of the
method you are trying to optimize.

You have to write your compiler to reason about all of this stuff but also have
really bounded compile times. So you have heuristics.

I did a survey of a bunch of compilers, mostly JIT compilers, to see what their
inlining heuristics look like. I also read (skimmed) some papers to see what
those folks had to say. I wonder if they agree.

## The survey

We'll start with [Cinder][cinder], because when I wrote it I added only the
simplest heuristics, mostly "don't inline" signals. Over time, after I left,
people tuned it a bit more.

[cinder]: https://github.com/facebookincubator/cinderx

### Cinder

The [inliner][cinder-inliner] starts from the caller CFG, walking it to find
suitable inlining candidates. Inlining candidates are only for call targets
that are known---in Cinder's case, only for monomorphic call targets---and pass
some checks. The callee is only known by it's function object, which includes
its bytecode. There is no IR available for the callee.

[cinder-inliner]: https://github.com/facebookincubator/cinderx/blob/88189ebf4bfd196ac7578c5076efa39bfa11f211/cinderx/Jit/hir/inliner.cpp#L341

Most of the "can't handle this" checks are related to argument handling. Python
has a pretty complex calling convention, so if the caller/callee have not
agreed on how the arguments should be passed through, the inliner doesn't care
to try and figure it out on its own. That is the responsibility of [other parts
of the compiler][cinder-resolve-args].

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
  if (code->co_flags & CO_VARARGS) {
    return fail(InlineFailureType::kHasVarargs);
  }
  if (code->co_flags & CO_VARKEYWORDS) {
    return fail(InlineFailureType::kHasVarkwargs);
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

#### V8 Hydrogen

Inlining happens during Hydrogen graph building
* https://github.com/tekknolagi/v8/blob/a969ab67f8e1e7475d9b26468225c3a772890c64/src/crankshaft/hydrogen.cc#L9236

Don't store function bytecode of all functions; need to re-parse callee *text
source* to inline

Heuristics https://github.com/tekknolagi/v8/blob/a969ab67f8e1e7475d9b26468225c3a772890c64/src/crankshaft/hydrogen.cc#L7807
* something about native context
* check callee AST size against configurable limit
* check inlining depth against configurable limit
* don't inline recursive functions
* check current cumulative method size (as tracked by AST node count) against
  configurable limit
*

V8 TurboFan
https://docs.google.com/document/d/1VoYBhpDhJC4VlqMXCKvae-8IGuheBGxy32EOgC2LnT8/edit
https://github.com/v8/v8/blob/036842f4841326130a40adfcff38f85a9b4cd30a/src/compiler/js-inlining-heuristic.h#L14

V8 Maglev
https://github.com/v8/v8/blob/036842f4841326130a40adfcff38f85a9b4cd30a/src/maglev/maglev-inlining.h#L36

```c++
bool MaglevInliner::CanInlineCall() {
  return !graph_->inlineable_calls().empty() &&
         (graph_->total_inlined_bytecode_size() <
              max_inlined_bytecode_size_cumulative() ||
          graph_->total_inlined_bytecode_size_small() <
              max_inlined_bytecode_size_small_total());
}
```

```c++
bool MaglevInliner::InlineCallSites() {
  DCHECK(CanInlineCall());
  while (!graph_->inlineable_calls().empty()) {
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
    if (result == InliningResult::kAbort) return false;
    if (result == InliningResult::kFail) continue;
    DCHECK_EQ(result, InliningResult::kDone);

    // Remove unreachable blocks if we have any.
    if (graph_->may_have_unreachable_blocks()) {
      graph_->RemoveUnreachableBlocks();
    }
  }
  return true;
}
```

```c++
bool MaglevInliner::Run() {
  if (graph_->inlineable_calls().empty()) return true;

  while (CanInlineCall()) {
    if (!InlineCallSites()) return false;
    RunOptimizer();
  }

  // Clear conversion, identities and ReturnedValues uses from deopt frames.
  // ...
  return true;
}
```

`bool MaglevGraphBuilder::CanInlineCall(compiler::SharedFunctionInfoRef shared, float call_frequency) {`

`bool MaglevGraphBuilder::ShouldEagerInlineCall(`

`MaybeReduceResult MaglevGraphBuilder::TryBuildCallKnownJSFunction(`

Unclear how `inlineable_calls` is populated... jk it's in `MaybeReduceResult MaglevGraphBuilder::TryBuildInlineCall(`

### JavaScriptCore

* Bytecode inlining
  * https://github.com/WebKit/WebKit/blob/709c3895afd71e0836f8c8be7393e44d41fab7e1/Source/JavaScriptCore/bytecode/CodeBlock.cpp#L2453
* DFG
  * https://github.com/WebKit/WebKit/blob/709c3895afd71e0836f8c8be7393e44d41fab7e1/Source/JavaScriptCore/dfg/DFGCapabilities.cpp#L76

JSC only inlines based on bytecode profile information, and only inlines
bytecode??

<!--
Compile plan
https://github.com/WebKit/WebKit/blob/709c3895afd71e0836f8c8be7393e44d41fab7e1/Source/JavaScriptCore/dfg/DFGPlan.cpp#L186
-->

### SpiderMonkey

Wasm
https://github.com/mozilla-firefox/firefox/blob/438a3ce10eb77fb50d968463b7741117aec5bb4a/js/src/wasm/WasmHeuristics.h#L213

SpiderMonkey ICScript

### Wasmtime and Cranelift

https://fitzgen.com/2025/11/19/inliner.html

### HotSpot

HotSpot C2
https://github.com/openjdk/jdk/blob/a05d5d2514c835f2bfeaf7a8c7df0ac241f0177f/src/hotspot/share/opto/bytecodeInfo.cpp#L116
https://github.com/openjdk/jdk/blob/497dca2549a9829530670576115bf4b8fab386b3/src/hotspot/share/opto/bytecodeInfo.cpp#L197
https://github.com/openjdk/jdk/blob/497dca2549a9829530670576115bf4b8fab386b3/src/hotspot/share/opto/parse.hpp#L42
https://github.com/openjdk/jdk/blob/497dca2549a9829530670576115bf4b8fab386b3/src/hotspot/share/opto/doCall.cpp#L185

Not too small

Walk up the call stack to figure out what to compile

Handling the right thing to inline: def foo(a) = a.each {|x| x }
want to compile `foo`, inline each, inline block, not compile block separately
(probably)

HotSpot C1
https://bernsteinbear.com/assets/img/design-hotspot-client-compiler.pdf
https://github.com/openjdk/jdk/blob/d854a04231a437a6af36ae65780961f40f336343/src/hotspot/share/c1/c1_GraphBuilder.cpp#L755
https://github.com/openjdk/jdk/blob/d854a04231a437a6af36ae65780961f40f336343/src/hotspot/share/c1/c1_GraphBuilder.cpp#L3854

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
https://ieeexplore.ieee.org/document/8661171

### .NET

https://github.com/dotnet/runtime/blob/2d638dc1179164a08d9387cbe6354fe2b7e4d823/docs/design/coreclr/jit/inlining-plans.md
https://github.com/dotnet/runtime/blob/0b3f3ab1ecf4de06459e5f0e2b7cb3baf70ef981/src/coreclr/jit/inline.def#L94
https://github.com/dotnet/runtime/blob/0b3f3ab1ecf4de06459e5f0e2b7cb3baf70ef981/src/coreclr/jit/inlinepolicy.cpp
https://github.com/dotnet/runtime/blob/0b3f3ab1ecf4de06459e5f0e2b7cb3baf70ef981/docs/design/coreclr/jit/inline-size-estimates.md?plain=1#L5
https://github.com/dotnet/runtime/blob/0b3f3ab1ecf4de06459e5f0e2b7cb3baf70ef981/src/coreclr/jit/fginline.cpp
https://github.com/dotnet/runtime/issues/10303
https://github.com/AndyAyersMS/PerformanceExplorer/blob/master/notes/notes-aug-2016.md
<!--
LSRA heuristics
https://github.com/dotnet/runtime/blob/2d638dc1179164a08d9387cbe6354fe2b7e4d823/docs/design/coreclr/jit/lsra-heuristic-tuning.md
-->

### Dart

https://github.com/dart-lang/sdk/blob/391212f3da8cc0790fc532d367549042216bd5ca/runtime/vm/compiler/backend/inliner.cc#L49
https://github.com/dart-lang/sdk/blob/391212f3da8cc0790fc532d367549042216bd5ca/runtime/vm/compiler/backend/inliner.cc#L1023
https://web.archive.org/web/20170830093403id_/https://link.springer.com/content/pdf/10.1007/978-3-540-78791-4_5.pdf

### HHVM

https://github.com/facebook/hhvm/blob/eeba7ad1ffa372a9b8cc9d1ec7f5295d45627009/hphp/runtime/vm/jit/inlining-decider.h#L89

### ART

https://github.com/LineageOS/android_art/blob/8ce603e0c68899bdfbc9cd4c50dcc65bbf777982/compiler/optimizing/inliner.h

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

### Maxine

### JikesRVM

### Other/research

Partial inlining

"optimal inlining"
https://ethz.ch/content/dam/ethz/special-interest/infk/ast-dam/documents/Theodoridis-ASPLOS22-Inlining-Paper.pdf

machine learning
https://ieeexplore.ieee.org/document/6495004
https://ssw.jku.at/Teaching/PhDTheses/Mosaner/Dissertation%20Mosaner.pdf

Other
https://webdocs.cs.ualberta.ca/~amaral/thesis/ErickOchoaMSc.pdf
https://karimali.ca/resources/papers/ourinliner.pdf
https://dl.acm.org/doi/10.1145/182409.182489
https://github.com/chrisseaton/rhizome/blob/main/doc/inlining.md
http://aleksandar-prokopec.com/resources/docs/prio-inliner-final.pdf
https://www.cresco.enea.it/SC05/schedule/pdf/pap274.pdf
https://dl.acm.org/doi/pdf/10.1145/3563838.3567677
clusters from https://llvm.org/devmtg/2022-05/slides/2022EuroLLVM-CustomBenefitDrivenInliner-in-FalconJIT.pdf


### Graal

https://github.com/oracle/graal/blob/5dde777cba22a99ebe3f19745d03ddfbc35c563c/compiler/src/jdk.graal.compiler/src/jdk/graal/compiler/phases/common/inlining/policy/GreedyInliningPolicy.java
https://github.com/oracle/graal/blob/5dde777cba22a99ebe3f19745d03ddfbc35c563c/compiler/src/jdk.graal.compiler/src/jdk/graal/compiler/phases/common/inlining/InliningPhase.java
https://github.com/oracle/graal/blob/5dde777cba22a99ebe3f19745d03ddfbc35c563c/compiler/src/jdk.graal.compiler/src/jdk/graal/compiler/phases/common/inlining/info/elem/InlineableGraph.java#L148
<!--
GVN
https://github.com/oracle/graal/blob/5dde777cba22a99ebe3f19745d03ddfbc35c563c/compiler/src/jdk.graal.compiler/src/jdk/graal/compiler/phases/common/DominatorBasedGlobalValueNumberingPhase.java#L132
-->

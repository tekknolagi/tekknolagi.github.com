---
title: A catalog of side effects
layout: post
---

Compilers like to keep track of each IR instruction's *effects*. An
instruction's effects vary wildly from having no effects at all, to writing a
specific variable, to completely unknown (writing all state).

Different compilers represent and track these effects differently. I've been
thinking about how to represent these effects all year, so I have been doing
some reading. In this post I will give some summaries of the landscape of
approaches. Please feel free to suggest more.

## Some background

Internal IR effect tracking is similar to the programming language notion of
algebraic effects in type systems, but internally, compilers keep track of
finer-grained effects. These effects indicate what instructions can be
re-ordered, duplicated, or removed entirely.

For example, consider the following pseodocode for some made-up language that
stands in for a snippet of compiler IR:

```python
# ...
a = l[0]
l[0] = 5
# ...
```

The goal of effects is to communicate to the compiler that these two IR
instructions *cannot be re-ordered*. The second instruction writes to a
location that the first one reads.

Different compilers keep track of this information differently. The null
effect analysis gives up and says "we can't re-order or delete any
instructions". That's probably fine for a first stab at a compiler, where you
will get a big speed up purely based on strength reductions.

But at some point you start wanting to do dead code elimination (DCE), or
common subexpression elimination (CSE), or move instructions around, and you
start wondering how to represent effects. That's where I am right now. So
here's a catalog of different compilers I have looked at recently.

## Cinder

We'll start with [Cinder][cinder], a Python JIT, because that's what I used to
work on. Cinder tracks heap effects for its high-level IR (HIR) in
[instr_effects.h][cinder-instr-effects-h]. Pretty much everything happens in
the `memoryEffects(const Instr& instr)` function, which is expected to know
everything about what effects the given instruction might have.

[cinder]: https://github.com/facebookincubator/cinder
[cinder-instr-effects-h]: https://github.com/facebookincubator/cinderx/blob/8bf5af94e2792d3fd386ab25b1aeedae27276d50/cinderx/Jit/hir/instr_effects.h

The data representation is a bitset representation of a lattice called an
`AliasClass` and that is defined in [alias_class.h][cinder-alias-class-h]. Each
bit in the bitset represents a distinct location in the heap: reads from and
writes to each of these locations are guaranteed not to affect any of the other
locations.

[cinder-alias-class-h]: https://github.com/facebookincubator/cinderx/blob/8bf5af94e2792d3fd386ab25b1aeedae27276d50/cinderx/Jit/hir/alias_class.h

Here is the X-macro that defines it:

```c
#define HIR_BASIC_ACLS(X) \
  X(ArrayItem)            \
  X(CellItem)             \
  X(DictItem)             \
  X(FuncArgs)             \
  X(FuncAttr)             \
  X(Global)               \
  X(InObjectAttr)         \
  X(ListItem)             \
  X(Other)                \
  X(TupleItem)            \
  X(TypeAttrCache)        \
  X(TypeMethodCache)

enum BitIndexes {
#define ACLS(name) k##name##Bit,
    HIR_BASIC_ACLS(ACLS)
#undef ACLS
};
```

Note that each bit implicitly represents a set: `ListItem` does not refer to a
*specific* list index, but the infinite set of all possible list indices. It's
*any* list item. Still, every list item is completely disjoint from, say, every
global variable.

Like other bitset lattices, it's possible to union the sets by or-ing the bits.
If this sounds familiar, it's because (as the repo notes) it's a similar idea
to Cinder's [type lattice representation](/blog/lattice-bitset/).

And, like other lattices, there is both a bottom element (no effects) and a
top element (all possible effects):

```c
#define HIR_OR_BITS(name) | k##name

#define HIR_UNION_ACLS(X)                           \
  /* Bottom union */                                \
  X(Empty, 0)                                       \
  /* Top union */                                   \
  X(Any, 0 HIR_BASIC_ACLS(HIR_OR_BITS))             \
  /* Memory locations accessible by managed code */ \
  X(ManagedHeapAny, kAny & ~kFuncArgs)
```

All of this together lets the optimizer ask and answer questions such as:

* where might this instruction write?
* where does this instruction borrow its input from?
* do these two instructions write destinations overlap?

and more.

These memory effects could in the future be used for instruction re-ordering,
but they are mostly used in two places in Cinder: the refcount insertion pass
and DCE.

## HHVM

[HHVM](https://github.com/facebook/hhvm), a JIT for the
[Hack](https://hacklang.org/) language, also uses a bitset for its memory
effects. See for example: [alias-class.h][hhvm-alias-class-h] and
[memory-effects.h][hhvm-memory-effects-h].

[hhvm-alias-class-h]: https://github.com/facebook/hhvm/blob/0395507623c2c08afc1d54c0c2e72bc8a3bd87f1/hphp/runtime/vm/jit/alias-class.h
[hhvm-memory-effects-h]: https://github.com/facebook/hhvm/blob/0395507623c2c08afc1d54c0c2e72bc8a3bd87f1/hphp/runtime/vm/jit/memory-effects.h

HHVM has a couple places that uses this information, such as [a
definition-sinking pass][hhvm-def-sink-cpp], [alias
analysis][hhvm-alias-analysis-h], [DCE][hhvm-dce-cpp], [store
elimination][hhvm-store-elim-cpp], and more.

[hhvm-def-sink-cpp]: https://github.com/facebook/hhvm/blob/4cdb85bf737450bf6cb837d3167718993f9170d7/hphp/runtime/vm/jit/def-sink.cpp
[hhvm-alias-analysis-h]: https://github.com/facebook/hhvm/blob/0395507623c2c08afc1d54c0c2e72bc8a3bd87f1/hphp/runtime/vm/jit/alias-analysis.h
[hhvm-dce-cpp]: https://github.com/facebook/hhvm/blob/4cdb85bf737450bf6cb837d3167718993f9170d7/hphp/runtime/vm/jit/dce.cpp
[hhvm-store-elim-cpp]: https://github.com/facebook/hhvm/blob/4cdb85bf737450bf6cb837d3167718993f9170d7/hphp/runtime/vm/jit/store-elim.cpp

If you are wondering why the HHVM representation looks similar to the Cinder
representation, it's because some former HHVM engineers such as Brett Simmers
also worked on Cinder!

## Android ART

(note that I am linking an ART fork on GitHub as a reference, but the upstream
code is [hosted on googlesource][googlesource-art])

[googlesource-art]: https://android.googlesource.com/platform/art/+/refs/heads/main/compiler/optimizing/nodes.h

Android's [ART Java runtime](https://source.android.com/docs/core/runtime) also
uses a bitset for its effect representation. It's a very compact class called
`SideEffects` in [nodes.h][art-nodes-h].

[art-nodes-h]: https://github.com/LineageOS/android_art/blob/c09a5c724799afdc5f89071b682b181c0bd23099/compiler/optimizing/nodes.h#L1602

The side effects are used in [loop-invariant code motion][art-licm-cc], [global
value numbering][art-gvn-cc], [write barrier
elimination][art-write-barrier-elimination-cc], [scheduling][art-scheduler-cc],
and more.

[art-licm-cc]: https://github.com/LineageOS/android_art/blob/c09a5c724799afdc5f89071b682b181c0bd23099/compiler/optimizing/licm.cc#L104
[art-gvn-cc]: https://github.com/LineageOS/android_art/blob/c09a5c724799afdc5f89071b682b181c0bd23099/compiler/optimizing/gvn.cc#L204
[art-write-barrier-elimination-cc]: https://github.com/LineageOS/android_art/blob/c09a5c724799afdc5f89071b682b181c0bd23099/compiler/optimizing/write_barrier_elimination.cc#L45
[art-scheduler-cc]: https://github.com/LineageOS/android_art/blob/c09a5c724799afdc5f89071b682b181c0bd23099/compiler/optimizing/scheduler.cc#L55

## JavaScriptCore

I keep coming back to [How I implement SSA form][pizlo-ssa] by [Fil
Pizlo][pizlo]. In particular, I keep coming back to the [Uniform Effect
Representation][pizlo-effect] section. This notion of "abstract heaps" felt
very... well, abstract. The pre-order and post-order integer pair as a way to
represent nested heap effects just did not click.

[pizlo]: http://www.filpizlo.com/
[pizlo-ssa]: https://gist.github.com/pizlonator/cf1e72b8600b1437dda8153ea3fdb963
[pizlo-effect]: https://gist.github.com/pizlonator/cf1e72b8600b1437dda8153ea3fdb963#uniform-effect-representation

It didn't really make sense until I actually went spelunking in JavaScriptCore
and found one of several implementations.



## Let's look at some compilers

B3 from JSC
https://github.com/WebKit/WebKit/blob/main/Source/JavaScriptCore/b3/B3Effects.h
https://github.com/WebKit/WebKit/blob/5811a5ad27100acab51f1d5ba4518eed86bbf00b/Source/JavaScriptCore/b3/B3AbstractHeapRepository.h

DOMJIT from JSC
https://github.com/WebKit/WebKit/blob/main/Source/WebCore/domjit/generate-abstract-heap.rb
generates from https://github.com/WebKit/WebKit/blob/b99cb96a7a3e5978b475d2365b72196e15a1a326/Source/WebCore/domjit/DOMJITAbstractHeapRepository.yaml#L4

DFG from JSC
https://github.com/WebKit/WebKit/blob/b99cb96a7a3e5978b475d2365b72196e15a1a326/Source/JavaScriptCore/dfg/DFGAbstractHeap.h
https://github.com/WebKit/WebKit/blob/b99cb96a7a3e5978b475d2365b72196e15a1a326/Source/JavaScriptCore/dfg/DFGClobberize.h
https://github.com/WebKit/WebKit/blob/b99cb96a7a3e5978b475d2365b72196e15a1a326/Source/JavaScriptCore/dfg/DFGClobberize.cpp
https://github.com/WebKit/WebKit/blob/b99cb96a7a3e5978b475d2365b72196e15a1a326/Source/JavaScriptCore/dfg/DFGClobberize.h
https://github.com/WebKit/WebKit/blob/b99cb96a7a3e5978b475d2365b72196e15a1a326/Source/JavaScriptCore/dfg/DFGStructureAbstractValue.cpp
https://github.com/WebKit/WebKit/blob/b99cb96a7a3e5978b475d2365b72196e15a1a326/Source/JavaScriptCore/dfg/DFGStructureAbstractValue.h
https://github.com/WebKit/WebKit/blob/b99cb96a7a3e5978b475d2365b72196e15a1a326/Source/JavaScriptCore/dfg/DFGClobberSet.h
https://github.com/WebKit/WebKit/blob/b99cb96a7a3e5978b475d2365b72196e15a1a326/Source/JavaScriptCore/dfg/DFGStructureAbstractValue.h

Fil Pizlo SSA doc (inspired by TBAA)
https://gist.github.com/pizlonator/cf1e72b8600b1437dda8153ea3fdb963

LLVM
https://llvm.org/docs/LangRef.html#tbaa-metadata

LLVM MemorySSA
https://llvm.org/docs/MemorySSA.html

MEMOIR
https://conf.researchr.org/details/cgo-2024/cgo-2024-main-conference/31/Representing-Data-Collections-in-an-SSA-Form

Scala LMS graph IR
https://2023.splashcon.org/details/splash-2023-oopsla/46/Graph-IRs-for-Impure-Higher-Order-Languages-Making-Aggressive-Optimizations-Affordab

V8 Turboshaft
https://github.com/v8/v8/blob/e817fdf31a2947b2105bd665067d92282e4b4d59/src/compiler/turboshaft/operations.h#L618

Guile Scheme
https://wingolog.org/archives/2014/05/18/effects-analysis-in-guile

Dotnet/CoreCLR
https://github.com/dotnet/runtime/blob/a0878687d02b42034f4ea433ddd7a72b741510b8/src/coreclr/jit/sideeffects.h#L169

Simple
https://github.com/SeaOfNodes/Simple/tree/main/chapter10

MIR and borrow checker
https://rustc-dev-guide.rust-lang.org/part-3-intro.html#source-code-representation

> "Fabrice Rastello, Florent Bouchez Tichadou (2022) SSA-based Compiler Design"--most (all?) chapters in Part III, Extensions, are pretty much motivated by doing alias analysis in some way

Intermediate Representations in Imperative Compilers: A Survey
http://kameken.clique.jp/Lectures/Lectures2013/Compiler2013/a26-stanier.pdf

Partitioned Lattice per Variable (PLV) -- that's in Chapter 13 on SSI

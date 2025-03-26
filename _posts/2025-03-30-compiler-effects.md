---
title: A catalog of side effects
layout: post
---

Compilers like to keep track of each IR instruction's *effects*. This effect
tracking is similar to the programming language notion of algebraic effects in
type systems, but internally, compilers keep track of way more fine-grained
effects. Thes effects indicate what instructions can be re-ordered, duplicated,
or removed entirely.

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

But at some point you start wanting to do dead code elimination (DCE), or move
instructions around, and you start wondering how to represent effects. That's
where I am right now. So here's a catalog of different compilers I have looked
at recently.

## Let's look at some compilers

Cinder
https://github.com/facebookincubator/cinderx/blob/b1be7e9c33a0023a0dee1f3a23e35a8810e00ae9/Jit/hir/instr_effects.h

B3 from JSC
https://github.com/WebKit/WebKit/blob/main/Source/JavaScriptCore/b3/B3Effects.h

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

HHVM
https://github.com/facebook/hhvm/blob/0395507623c2c08afc1d54c0c2e72bc8a3bd87f1/hphp/runtime/vm/jit/memory-effects.h

Android ART
https://github.com/LineageOS/android_art/blob/0abc6e9ecd0bdf2414966af6f9e850a05f88413f/compiler/optimizing/nodes.h#L1850

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

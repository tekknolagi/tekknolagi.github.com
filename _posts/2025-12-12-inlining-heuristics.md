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

V8 Hydrogen
https://github.com/tekknolagi/v8/blob/a969ab67f8e1e7475d9b26468225c3a772890c64/src/crankshaft/hydrogen.cc#L7807

V8 TurboFan
https://docs.google.com/document/d/1VoYBhpDhJC4VlqMXCKvae-8IGuheBGxy32EOgC2LnT8/edit
https://github.com/v8/v8/blob/036842f4841326130a40adfcff38f85a9b4cd30a/src/compiler/js-inlining-heuristic.h#L14

V8 Maglev
https://github.com/v8/v8/blob/036842f4841326130a40adfcff38f85a9b4cd30a/src/maglev/maglev-inlining.h#L36

HotSpot C2
https://github.com/openjdk/jdk/blob/a05d5d2514c835f2bfeaf7a8c7df0ac241f0177f/src/hotspot/share/opto/bytecodeInfo.cpp#L116

Not too small

Walk up the call stack to figure out what to compile

Handling the right thing to inline: def foo(a) = a.each {|x| x }
want to compile `foo`, inline each, inline block, not compile block separately
(probably)

TruffleRuby uses weighted compile queue

HotSpot C1
https://bernsteinbear.com/assets/img/design-hotspot-client-compiler.pdf
https://github.com/openjdk/jdk/blob/d854a04231a437a6af36ae65780961f40f336343/src/hotspot/share/c1/c1_GraphBuilder.cpp#L755
https://github.com/openjdk/jdk/blob/d854a04231a437a6af36ae65780961f40f336343/src/hotspot/share/c1/c1_GraphBuilder.cpp#L3854

SpiderMonkey Wasm
https://github.com/mozilla-firefox/firefox/blob/438a3ce10eb77fb50d968463b7741117aec5bb4a/js/src/wasm/WasmHeuristics.h#L213

SpiderMonkey ICScript

PyPy
"always"

Cinder
https://github.com/facebookincubator/cinderx/blob/ccb8e40a3509d9fdfe22870f56e8547562763067/cinderx/Jit/hir/inliner.cpp#L343

"optimal inlining"
https://ethz.ch/content/dam/ethz/special-interest/infk/ast-dam/documents/Theodoridis-ASPLOS22-Inlining-Paper.pdf

machine learning
https://ieeexplore.ieee.org/document/6495004
https://ssw.jku.at/Teaching/PhDTheses/Mosaner/Dissertation%20Mosaner.pdf

.NET
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

Graal
https://ieeexplore.ieee.org/document/8661171

Dart
https://github.com/dart-lang/sdk/blob/391212f3da8cc0790fc532d367549042216bd5ca/runtime/vm/compiler/backend/inliner.cc#L49
https://github.com/dart-lang/sdk/blob/391212f3da8cc0790fc532d367549042216bd5ca/runtime/vm/compiler/backend/inliner.cc#L1023
https://web.archive.org/web/20170830093403id_/https://link.springer.com/content/pdf/10.1007/978-3-540-78791-4_5.pdf

HHVM
https://github.com/facebook/hhvm/blob/eeba7ad1ffa372a9b8cc9d1ec7f5295d45627009/hphp/runtime/vm/jit/inlining-decider.h#L89

ART
https://github.com/LineageOS/android_art/blob/8ce603e0c68899bdfbc9cd4c50dcc65bbf777982/compiler/optimizing/inliner.h

Other
https://webdocs.cs.ualberta.ca/~amaral/thesis/ErickOchoaMSc.pdf
https://karimali.ca/resources/papers/ourinliner.pdf
https://dl.acm.org/doi/10.1145/182409.182489
https://github.com/chrisseaton/rhizome/blob/main/doc/inlining.md
http://aleksandar-prokopec.com/resources/docs/prio-inliner-final.pdf
https://www.cresco.enea.it/SC05/schedule/pdf/pap274.pdf
https://dl.acm.org/doi/pdf/10.1145/3563838.3567677
clusters from https://llvm.org/devmtg/2022-05/slides/2022EuroLLVM-CustomBenefitDrivenInliner-in-FalconJIT.pdf

Maxine

JikesRVM

Partial inlining

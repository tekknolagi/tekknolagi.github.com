---
title: "A survey of inlining heuristics"
layout: post
---

V8 TurboFan
https://docs.google.com/document/d/1VoYBhpDhJC4VlqMXCKvae-8IGuheBGxy32EOgC2LnT8/edit
https://github.com/v8/v8/blob/036842f4841326130a40adfcff38f85a9b4cd30a/src/compiler/js-inlining-heuristic.h#L14

HotSpot C2
https://github.com/openjdk/jdk/blob/a05d5d2514c835f2bfeaf7a8c7df0ac241f0177f/src/hotspot/share/opto/bytecodeInfo.cpp#L116

HotSpot C1
https://github.com/openjdk/jdk/blob/d854a04231a437a6af36ae65780961f40f336343/src/hotspot/share/c1/c1_GraphBuilder.cpp#L755
https://github.com/openjdk/jdk/blob/d854a04231a437a6af36ae65780961f40f336343/src/hotspot/share/c1/c1_GraphBuilder.cpp#L3854

SpiderMonkey Wasm
https://github.com/mozilla-firefox/firefox/blob/438a3ce10eb77fb50d968463b7741117aec5bb4a/js/src/wasm/WasmHeuristics.h#L213

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
https://github.com/dotnet/runtime/blob/0b3f3ab1ecf4de06459e5f0e2b7cb3baf70ef981/src/coreclr/jit/inline.def#L94
https://github.com/dotnet/runtime/blob/0b3f3ab1ecf4de06459e5f0e2b7cb3baf70ef981/src/coreclr/jit/inlinepolicy.cpp
https://github.com/dotnet/runtime/blob/0b3f3ab1ecf4de06459e5f0e2b7cb3baf70ef981/docs/design/coreclr/jit/inline-size-estimates.md?plain=1#L5
https://github.com/dotnet/runtime/blob/0b3f3ab1ecf4de06459e5f0e2b7cb3baf70ef981/src/coreclr/jit/fginline.cpp

Graal
https://ieeexplore.ieee.org/document/8661171

Dart
https://github.com/dart-lang/sdk/blob/391212f3da8cc0790fc532d367549042216bd5ca/runtime/vm/compiler/backend/inliner.cc#L49

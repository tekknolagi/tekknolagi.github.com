---
title: Watching now
layout: page
---

Here are some projects that are occupying some brain space and that I am keeping an eye on, in no particular order (to demonstrate that, I shuffled before publishing). Some are because they do a lot with a small amount of code/people, some are because they feel like they are poking at something very new and cool, and some are excellent learning resources.

* [Roc](https://github.com/roc-lang/roc), a fast, friendly programming language
* [YJIT](https://github.com/Shopify/ruby), a JIT for Ruby based on Maxime's research on basic block versioning
* [bril](https://github.com/sampsyo/bril), an educational compiler IR
* [AtomVM](https://github.com/atomvm/AtomVM), a small BEAM (Erlang) VM for small devices
* [onramp](https://github.com/ludocode/onramp), a bootstrapping C compiler
* [wild](https://github.com/davidlattimore/wild), an incremental linker
* [Taskflow](https://github.com/taskflow/taskflow), a graph-based parallel task system
* [Alan](https://github.com/alantech/alan), an autoscalable programming language
* [elfconv](https://github.com/yomaytk/elfconv), an ELF to Wasm AOT compiler
* [bifrost](https://github.com/aperturerobotics/bifrost), a p2p library that supports multiple transports
* [dulwich](https://github.com/jelmer/dulwich), a pure Python Git implementation
* [HVM](https://github.com/HigherOrderCO/HVM), a massively parallel interaction combinator VM for the GPU
* [Pyret](https://github.com/brownplt/pyret-lang), a functional programming language with some excellent ideas and a great pedagogical focus
* [ir](https://github.com/dstogov/ir), the JIT internals for PHP
* [Iroh](https://github.com/n0-computer/iroh), a toolkit for building distributed applications
* [simple-abstract-interpreter](https://github.com/sree314/simple-abstract-interpreter), just what it says on the tin
* [ssa-optimizer](https://github.com/chrim05/ssa-optimizer), an educational SSA-based optimizer
* [hindley-milner-python](git@github.com:rob-smallshire/hindley-milner-python.git), a small Damas Hindley Milner implementation in Python
* [Hindley–Milner in Python](https://github.com/milesbarr/hindley-milner-in-python), another one of the same (but this one is a bit "trickier")
* [micrograd](https://github.com/karpathy/micrograd/), [ccml](https://github.com/t4minka/ccml), and [cccc](https://github.com/skeeto/cccc), small, educational autodiff libraries
* [Natalie](https://github.com/natalie-lang/natalie), an AOT Ruby compiler
* [plzoo](https://github.com/andrejbauer/plzoo), which has multiple different small PL implementations with different semantics
* [tinygrad](https://github.com/tinygrad/tinygrad), a small autodiff library with big plans
* [MaPLe compiper](https://github.com/MPLLang/mpl), a compiler for automatic fork/join parallelism in SML
* [try](https://github.com/binpash/try), to inspect a command's effects before modifying your live system
* [mold](https://github.com/rui314/mold/), a fast and parallel linker
* [monoruby](https://github.com/sisshiki1969/monoruby), a full Ruby VM and JIT by one person
* [MicroHs](https://github.com/augustss/MicroHs), Haskell implemented with combinators in a small C++ core
* [Cake](https://github.com/thradams/cake), a C23 frontend and compiler to older C versions
* [Toit](https://github.com/toitlang/toit), a VM from the Strongtalk lineage and management software for fleets of ESP32s
* [Cosmopolitan](https://github.com/jart/cosmopolitan), a C library that can build very portable self-contained binaries
* [gf](https://github.com/nakst/gf), a very usable GUI GDB frontend
* [MatMul-free LLM](https://github.com/ridgerchu/matmulfreellm), an implementation of transformer-based large language models without matrix multiplication
* [Bun](https://github.com/oven-sh/bun/), a fast JS runtime based on JavaScriptCore
* [Porffor](https://github.com/CanadaHonk/porffor), a from-scratch AOT JS engine
* [weval](https://github.com/cfallin/weval), a WebAssembly partial evaluator
* [Riker](https://github.com/curtsinger-lab/riker), correct and fast incremental builds using system call tracing
* [arcan](https://github.com/letoram/arcan), a new display server and window system
* [container2wasm](https://github.com/ktock/container2wasm), to run containers in WebAssembly
* [LPython](https://github.com/lcompilers/lpython), a very early stages optimizing Python compiler
* [Pydrofoil](https://github.com/pydrofoil/pydrofoil), a fast RISC-V emulator based on RPython, the PyPy internals
* [Fil-C](https://github.com/pizlonator/llvm-project-deluge), a project where Fil Pizlo is making a memory-safe version of C
* [Oil shell](https://github.com/oilshell/oil), a new Bash-compatible shell with fresh ideas and its own Python-esque compiler
* [bigint](https://github.com/983/bigint), a small arbitrary precision integer library for C
* [chibicc](https://github.com/rui314/chibicc)
* [bcgen](https://github.com/Kimplul/bcgen), which is kind of like Ertl's VMGen, and [copyjit](https://github.com/Kimplul/copyjit), which is like Copy and Patch
* [joos](https://github.com/just-js/joos), a KVM virtual machine manager in JavaScript

Not quite code but presenting very cool ideas:

* Verifying your whole register allocator too hard? No problem, just [write a verifier for a given allocation](https://cfallin.org/blog/2021/03/15/cranelift-isel-3/) and abort if it fails. This also lends itself nicely to fuzzing for automatically exploring large program state spaces.
* [Copy and Patch](https://fredrikbk.com/publications/copy-and-patch.pdf) (PDF) compilation, which generates pretty fast code very quickly
* [Egg](https://egraphs-good.github.io/), and more broadly egraphs, for program IRs
* [Implementing a Toy Optimizer](https://www.pypy.org/posts/2022/07/toy-optimizer.html) and union-find in general
* (To-be-written) Using Z3 to prove your static analyzer correct
  * [More Stupid Z3Py Tricks: Simple Proofs](http://www.philipzucker.com/more-stupid-z3py-tricks-simple-proofs/) is a good jumping-off point, as is [Compiling with Constraints](https://www.philipzucker.com/compile_constraints/)
* (To-be-written) From union-find to egraphs; exploring the tradeoffs
* [a simple semi-space collector](https://wingolog.org/archives/2022/12/10/a-simple-semi-space-collector) explains how semi-space GCs work in ~100 lines of C
  * Note: the zero-length array member `payload` is non-standard and probably not needed
  * Note: `is_forwarded` should actually check if if the masked tag is `== 0`
* [just-in-time code generation within webassembly](https://wingolog.org/archives/2022/08/18/just-in-time-code-generation-within-webassembly) and its accompanying [wasm-jit](https://github.com/wingo/wasm-jit)
* [Regular Expression Matching: the Virtual Machine Approach](https://swtch.com/~rsc/regexp/regexp2.html)
* Interaction nets and HVM
* [Make Your Self](https://marianoguerra.org/posts/make-your-self/)
* Cheney list copying / GC
    * Cheney on the MTA
* Escape analysis and dead code elimination as compile-time GC
* PGO for DCE: [tweet one](https://x.com/rui314/status/1788079197141049825) and [tweet two](https://x.com/rui314/status/1788099027889979782)
* Nostr, but without all the blockchain stuff

What I am working on:

* [Scrapscript](https://github.com/tekknolagi/scrapscript), an interpreter and compiler for a small functional language
* [Dr Wenowdis](https://bernsteinbear.com/assets/img/dr-wenowdis.pdf) (PDF), where CF Bolz-Tereick and I are working on making C extensions faster in PyPy
* A small rasterizer for my graphics class that uses [fenster](https://github.com/zserge/fenster)
* [weval](https://github.com/cfallin/weval)-ing CPython
* [Introduction to Software Development Tooling](https://bernsteinbear.com/isdt/)

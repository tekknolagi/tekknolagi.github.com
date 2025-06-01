---
layout: page
title: Some computing notes
permalink: /opfn/
---

WebAssembly (Wasm)

* [Fast in-place interpreter](https://dl.acm.org/doi/abs/10.1145/3563311)
  * Fast interpreter technique for Wasm; no IR. For purposes of this page, I
    value this more for a way to build a small, IR-less interpreter than for
    generating an interpreter using a macro assembler.
* [Wasm-R3](https://dl.acm.org/doi/10.1145/3689787)
  * Instrument Wasm programs to track input/output; minimize replay traces up
    to 99%. Injects Wasm code, but could also be done as a wrapper module
    probably. Quasi-journaling?
* [wizer](https://github.com/bytecodealliance/wizer)
  * Start a Wasm program, pause it, and quiesce its entire state into a new
    Wasm module with a new entrypoint
* [polywasm](https://github.com/evanw/polywasm)
  * Compile wasm to JS. Bundled, 2500LOC JS for the compiler. Little WASI stub
    could be ~100LOC more.
* [memfs](https://github.com/tekknolagi/llvm-project/tree/5dc09c94393510bc8d042a9f07382b53e845c0f2/binji)
  * In-memory filesystem in C that can be compiled to Wasm
* [ASC](https://dl.acm.org/doi/10.1145/2654822.2541985)
  * Seltzer's group at Harvard build some wild stuff. Wonder if it would be
    easier with a smaller/more introspectable machine like Wasm.
* [wasmstore](https://github.com/dylibso/wasmstore) is content addressable Wasm
* [wasm-persist](https://github.com/dfinity-side-projects/wasm-persist) can
  hibernate a Wasm instance
* [Gate](https://github.com/gate-computer/gate) is content-addressable Wasm,
  RPC, and snapshot/restore

Little Wasms

* [Wasmbox](https://github.com/imasahiro/wasmbox)
* [tiny-wasm-runtime](https://github.com/r1ru/tiny-wasm-runtime)
* [winter](https://github.com/peterseymour/winter)
* [cjwasm?](https://github.com/jeaiii/cjwasm)
* [baseline wasm compiler](https://wingolog.org/archives/2020/03/25/firefoxs-low-latency-webassembly-compiler)
* [Whose baseline compiler is it anyway?](https://arxiv.org/abs/2305.13241)
* [wasm-interpreter](https://github.com/csjh/wasm-interpreter)

Other

* [Hermit](https://github.com/dylibso/hermit)
* [keyvm](https://github.com/void4/keyvm)
* [rarvm](https://github.com/void4/rarust)
* [mycelium](https://github.com/mycweb/mycelium)
* [want](https://github.com/wantbuild/want)

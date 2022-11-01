---
title: "Adding a symbolizer to the Cinder JIT"
layout: post
date: 2022-11-10
description: >
  Adding more names to debug information is always helpful.
---

I work on [Cinder](https://github.com/facebookincubator/cinder), a just-in-time
(JIT) compiler built on top of CPython. If you aren't familiar with Cinder and
want to learn more, [a previous post about the
inliner](/blog/cinder-jit-inliner/) gives a decent overview of the JIT. This
post will talk about our function symbolizer, why we added it, and how it
works.

## Motivation

* Want names in debug output
* Can pipe names all the way through but that's not always easy
* `dladdr` works for limited cases
* There are tables in ELF header
  * Can't read them because they are not loaded into memory
* Read own ELF header from disk
  * Note: Valgrind bug
* Symbols from shared objects are not there
  * This is a problem when Cinder is embedded as a .so in another application
  * Also a problem for naming symbols from .so that Cinder loads
* Read ELF header of each .so loaded
* Symbols are mangled, so demangle

## Requirements

* Linux and ELF

## Implementation details

## Other thoughts

## Similar work

### Abseil symbolizer

* https://github.com/abseil/abseil-cpp/blob/d819278ab70ee5e59fa91d76a66abeaa106b95c9/absl/debugging/symbolize_elf.inc

### Folly symbolizer

* https://github.com/facebook/folly/blob/74d381aacc02cfd892d394205f1e066c76e18e60/folly/experimental/symbolizer/Symbolizer.cpp
* https://github.com/facebook/folly/blob/74d381aacc02cfd892d394205f1e066c76e18e60/folly/experimental/symbolizer/Elf.cpp

### ClickHouse symbolizer

* https://github.com/ClickHouse/ClickHouse/blob/c8068bdfa260ccf486c2d0417b1eea9cbfb0ad59/src/Common/SymbolIndex.cpp

### HHVM symbolizer

* https://github.com/facebook/hhvm/blob/a2e15b83bd3ff360068dcf584264a42d85fe0c90/hphp/util/stack-trace.cpp
  * folly or libbfd


<hr style="width: 100px;" />
<!-- Footnotes -->

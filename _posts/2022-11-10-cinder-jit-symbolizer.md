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

If you notice something amiss, please let me know! Either send me an email,
post on [~max/blog-comments](https://lists.sr.ht/~max/blog-comments), or
comment on one of the various angry internet sites this will eventually get
posted to.

## Motivation

The JIT transforms Python bytecode to machine code. Along the way, we support
printing the intermediate representations (IRs) for debugging. We also support
disassembling the resulting machine code for the same reason.

The various IRs and machine code contain references to C and C++ functions by
address. While a running process only needs the address to go about its job,
software engineers like me need a little more than `0x3A28213A` to debug
things. This leaves us wanting a function that can go from address to function
name: a *symbolizer*.

You might wonder why we don't instead keep all of the names inside the
instructions. After all, we probably add the function pointers by name (like
`env.emit<CallCFunction>(PyNumber_Add, ...)`. Why not also add `"PyNumber_Add"`
alongside it?

I quite honestly do not have a good answer. I think it would take work to
thread all of that additional information through the system so that we can
guarantee it, but:

1. I already did this for the instructions that read from and write to fields.
   It's great.
2. Writing this symbolizer also took a lot of work.

In the end I decided to do what other projects like HHVM seem to do and wrote
the darn symbolizer.

## The journey

I wanted names in our debug output. Seeing stuff like
`CallCFunction<0x6339392C> v1 v7` was driving me batty.[^addresses] How am I
supposed to know what that represents? Sure, I can kind of make an inference
from the context, but it's not pleasant.

[^addresses]: Actually, it's worse than that. We didn't even print the
    addresses originally.

For some cases in the project we already used `dladdr` as a limited symbolizer.
Unfortunately, `dladdr` only works if the function is in some `.so` that your
application loaded. If you are trying to symbolize a function from your own
executable, you're out of luck.

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

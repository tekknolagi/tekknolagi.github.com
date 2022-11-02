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

To follow along, check out [symbolizer.cpp][symbolizer.cpp]. If you notice
something amiss, please let me know! Either send me an email, post on
[~max/blog-comments](https://lists.sr.ht/~max/blog-comments), or comment on one
of the various angry internet sites this will eventually get posted to.

[symbolizer.cpp]: https://github.com/facebookincubator/cinder/blob/ab2f6b5ca5274bbdd632b658cdce7de2274bfc56/Jit/symbolizer.cpp

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

I learned somewhere that at least for ELF binaries (and probably other
executable formats), there are names stored in the header. I had no idea how to
read my own ELF header. I tried to read from the start of the executable and
found an ELF header! It was great! And then I tried to read a section header
and got a segfault.

I learned (from [Employed Russian][employed-russian], as apparently
everybody who works on low-level things does) that section headers are not
loaded into memory at process start. Bummer. So how do we read the header?

[employed-russian]: https://stackoverflow.com/users/50617/employed-russian

Well, we loaded the executable from the disk on process boot. Why not read it
again? I went off to `mmap` the file `/proc/self/exe` so that I could read from
that instead.

I had some crashes, so I went to see if Valgrind could track down anything
weird for me. It turns out, though, that Valgrind [had a bug][valgrind-bug]
where it wouldn't intercept that `open` for the `mmap`, so actually I was
reading *Valgrind's* executable instead of my own when trying to track down my
memory error. At the time of symbolizer writing, the bug had been fixed, but I
did not have the latest version on hand.

[valgrind-bug]: https://bugzilla.redhat.com/show_bug.cgi?id=1925786

Through a bunch of trial and error and reading too much half-working code on
the internet and too many manual pages, I got the symbolizer working! I managed
to make it symbolize function names from our executable and fall back to
`dladdr` for symbols shared objects.

Problem solved, right? Nope:

* We also ship the JIT as a `.so`. If we reference a private symbol from the
  Cinder `.so`, our fancy symbol table walker won't be able to find it because
  it only reads from the executable. `dladdr` won't be able to resolve it
  either.
* Some other reason that I can't remember right now but it was irritating.

I borrowed some of our tech lead Matt Page's code for reading `.so`s and that
solved those problems. I'm not super sure why this code is different from the
code for reading the executable ELF header. Maybe they can be combined.

Then, finally, since we're using C++, we get fun mangled names. I used
`abi::__cxa_demangle` to get a nice readable name.

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

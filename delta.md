---
title: Delta debugging
layout: page
---

This page is meant to hold a collection of delta debugging tools and assorted
commentary because I am mentioning them in multiple places.

* I factored the non-JIT parts of Cinder's delta debugging implementation into
  a tiny drop-in [snippet of Python code](https://github.com/tekknolagi/omegastar).
  * ZJIT now has a [JIT bisect script](https://github.com/ruby/ruby/blob/2a6345e957c01f4495323723c7a3d7ac0d4ac339/tool/zjit_bisect.rb)
* Andrew Chambers wrote [a
  small implementation](https://github.com/andrewchambers/ddmin-python) that
  also includes a nice CLI
* [Hash-Based Bisect Debugging in Compilers and
  Runtimes](https://research.swtch.com/bisect) by Russ Cox

Other implementations of/similar to C-Reduce:

* [delta](https://github.com/dsw/delta) assists you in minimizing "interesting"
  files subject to a test of their interestingness
* [cvise](https://github.com/marxin/cvise), a super-parallel Python port of C-Reduce
* [Shrink Ray](https://github.com/DRMacIver/shrinkray), a modern multi-format test-case reducer
* [treereduce](https://langston-barrett.github.io/treereduce/), a fast,
  parallel, syntax-aware test case reducer based on tree-sitter grammars
* [halfempty](https://github.com/googleprojectzero/halfempty)
* [multidelta](https://manpages.ubuntu.com/manpages/bionic/man1/multidelta.1.html)

I saw somewhere (but can no longer find the link to) someone's implementation
of delta debugging on commit logs (more advanced than `git bisect`). If you
find this, please send it my way.

[Waleed Khan](https://blog.waleedkhan.name/) showed me his [cool
project](https://blog.waleedkhan.name/searching-source-control-graphs/) but I
don't think that was what I originally saw.

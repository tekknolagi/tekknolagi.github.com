---
title: "Support Local Variables"
layout: post
---

Discovering and supporting all of the wild semantics for local variables in
Ruby was so interesting that the ZJIT team decided to write a paper about it:
[Support Local Variables](/assets/img/support-local-variables.pdf) (PDF). It's
also the first bit of academic literature that documents and explains ZJIT.

It's published at VMIL 2026, where someone on our team will be presenting it.

**Abstract**

> Ruby is a dynamically typed and object-oriented programming language. Its
> primary implementation, CRuby, contains a bytecode virtual machine and a
> mature lazy basic block versioning (LBBV) just-in-time (JIT) compiler
> called YJIT.
>
> In order to both implement more advanced optimizations than YJIT supports
> and also encourage more outside contributions, we present a new
> method-based JIT called ZJIT. Like YJIT, ZJIT compiles from bytecode to
> machine code. Unlike YJIT, ZJIT has multiple global and local
> optimization passes.
>
> ZJIT's high-level intermediate representation is in static single
> assignment (SSA) form. In order to optimize Ruby's local variables, ZJIT
> lifts local variables into SSA values. This is a departure from how
> other Ruby compilers handle locals: other JIT compilers either leave local
> variables as memory loads and stores or do advanced partial evaluation to
> recover SSA values from memory.
>
> While implementing locals, we (re-)discovered what features make local
> variables in Ruby especially challenging to compile correctly and
> efficiently. We demonstrate these features and illustrate how we solved
> these problems in ZJIT.

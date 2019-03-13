---
title: The case for linearly bootstrapping compilers
layout: post
date: 2018-10-22 19:12:00 PDT
---

> "Recipe for yogurt: Add yogurt to milk."
> -Anon.

### Intro

I have been tossing an idea around for some time with friends of mine: linearly
bootstrapping compilers. This is a term we've coined to contrast the concept
with circularly bootstrapping compilers.

*Compiler:* A program that takes a program in one language (A) and transforms
it into an equivalent representation in another language (B). Generally used to
describe a transformation from a "higher level" representation to a "lower
level" representation.

*Bootstrapping compiler:* If language A has a compiler, the bootstrapping
compiler refers to a compiler of language A written itself in language A.

*Circularly bootstrapping compiler:* A language whose compiler is developed in
itself, multiple times over. If language A's compiler is originally written in
B (call the compiler A0), then the next generation of language A's compiler is
written in A0, and that next generation is called A1, and so on. See GCC, which
is written in C, or the Rust compiler, which is written in Rust.

*Linearly bootstrapping compiler:* Instead of language A's compiler being
developed in a cycle using the previous implementation of A

*Language implementation graph:*

*Verified subcomponent:*


<br />
<hr style="width: 100px;" />
<!-- Footnotes -->

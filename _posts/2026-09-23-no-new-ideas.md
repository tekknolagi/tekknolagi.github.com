---
title: "Dynamic language runtimes greatest hits"
layout: post
---

We're bringing up the ZJIT compiler and runtime right now. ZJIT is Ruby's
newest JIT compiler, brought to you by the same folks who built YJIT. It's
designed to be more conventional, more "textbook", and therefore encourage open
source contributions.

Creating a compiler requires making an implementation or architecture decision
almost every day. How do we represent code? What optimizations should we do? In
what order? How do we generate code from this? What should I have for the lunch
I forgot to eat? Things like that.

When we're thinking about these architectural decisions, one of my near-term
goals for ZJIT is to have *no new ideas*. Maybe in a couple of years we'll
develop some Very Interesting Technique and write Literature about it, but I
don't want to try to do that yet.

This is because if we have no new ideas, we'll have an *amazing* compiler.
Instead of having new ideas, I plan on taking advantage of the last 40 years of
dynamic language runtimes and compilers research, which has had a bountiful
harvest.

## People have been thinking about this for a long time

With every new dynamically typed programming language comes an optimized
implementation of said language. This has been going on for awhile. Here is a
much-abridged history:

1. John McCarthy created Lisp and saw that it was good. Tim Hart and Mike Levin
   saw Lisp and made a compiler. Thereafter many optimizing interpreters and
   compilers came to light.
1. PARC saw Lisp closures and wanted objects so they created Smalltalk. Then
   they wanted a faster Smalltalk, so they optimized it and called it
   Smalltalk-80. Things got out of hand. Now we have the OpenJDK.
1. Netscape created JavaScript. People got a little too eager to make websites
   dynamic. Now every web browser comes with at least three bug-ridden
   compilers of varying efficacy and [*The Birth &amp; Death of
   JavaScript*](https://www.destroyallsoftware.com/talks/the-birth-and-death-of-javascript)
   has been fully realized.
1. Even statically typed programming languages got some [interesting
   optimizations](/assets/img/tbaa.pdf) (PDF). Now we have to think about what
   it means to enable `-fstrict-aliasing`.
1. You have recently started to dream about how to optimize your implementation
   of the Lox programming language...

Many of the dynamic language efforts, both public and private, have resulted in
papers, blog posts, and half-hidden but illuminating scraps of code all across
the internet. And what's not already on the internet can be found with an
inter-library loan.

* If we have no new ideas, we'll have an amazing compiler
* Beg/borrow/steal
* The last 40 years of dynamic language runtimes research
* 10% of V8
* Shapes and polymorphism
* Always browsing other compilers and hlrz
* But Ruby is so different
  * Alluded to this in RubyKaigi talk but Ruby can kind of become just Java

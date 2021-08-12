---
title: Assembly interpreters
layout: post
date: 2021-03-20 00:00:00 PT
description: Writing a bytecode interpreter in x86-64 assembly
---

Welcome back to the fourth post in the runtime optimization series. The last
three posts were about [inline caching](/blog/inline-caching/),
[quickening](/blog/inline-caching-quickening), and [small
objects](/blog/small-objects/).

In this post, we will take another look at the interpreter loop. We will remove
some of the overhead present in the C version of the interpreter by writing the
loop and some opcode handlers in assembly.

## The problem

I alluded to some of the inefficiencies in our interpreter loop in previous
posts:

* `push` and `pop` read from and write to an auxiliary stack instead of the
  main program stack, which requires more instructions

Future direction:

* It enables some interpreter tricks like top of stack caching, which reduces
  memory traffic
* Calls become cheaper
* Pave the way for template JIT


* The C compiler isn't aware of the guarantees our object model provides, so
  while `ADD_INT` is much faster than the generic `ADD`, 

---
title: "ClassDistribution is really neat"
layout: post
---

One unassuming week of September 2022, Google Deepmind dropped a fully-fledged
[CPython JIT called S6](https://github.com/google-deepmind/s6) squashed to one
commit. I had heard nothing of its development even though I was working on
[Cinder](https://github.com/facebookincubator/cinder) at the time and generally
heard about new JIT efforts. I started poking at it.

The README has some excellent structural explanation of how they optimize
Python, including a nice introduction to hidden classes (also called shapes,
layouts, and maps elsewhere). Hidden classes are core to making dynamic
language runtimes fast: they allow for what is normally a hashtable lookup to
become an integer comparison and a memory load. (See a [great
tutorial](https://aosabook.org/en/500L/a-simple-object-model.html) by CF
Bolz-Tereick on how to build a hidden class based object model.)

Hidden classes give you the ability to more quickly read from objects, but you,
the runtime implementor, have to decide what kind of cache you want to use.
Should you have a monomorphic cache? Or a polymorphic cache?

## Inline caching and specialization

In an interpreter, a common approach is to do some kind of [state-machine-based
bytecode rewriting](/blog/inline-caching-quickening/). Your generic opcodes
(load an attribute, load a method, add) start off unspecialized, specialize to
monomorphic when they first observe a hidden class HC, rewrite themselves to
polymorphic when they observe the next hidden class HC', and may again rewrite
themselves to megamorphic (the sad case) when they see the K+1th hidden class.
Pure interpreters take this approach because they want to optimize as they go
and the unit of optimization is [normally](https://arxiv.org/pdf/2109.02958)
(PDF) one opcode at a time.

In an optimizing JIT world that cares a little less about interpreter/baseline
compiler performance, the monomorphic/polymorphic split may look a little
different:

1. monomorphic: generating code with a fixed hidden class ID to compare against
   and a fixed field offset to load from, and jumping into the interpreter if
   that very specific assumption is false
2. polymorphic: a self-modifying chain of such compare+load sequences, usually
   ending after some fixed number K entries with a jump into the interpreter

If you go for monomorphic and that code never sees any other hidden class,
you've won big: the generated code is small and generally you can use these
very strong type assumptions from having burned it into the code from the
beginning. If you're wrong, though, and the that ends up being a polymorphic
site in the code, you lose on performance: it will be constantly jumping into
the interpreter.

If you go for polymorphic but the code is mostly monomorphic

But "polymorphic" and "megamorphic" are very coarse summaries of the access
patterns at that site. Yes, side exits are slow, but if a call site S is
specialized only for hidden class HC and *mostly sees HC* but sometimes sees
HC', that's probably fine! We can take a few occasional side exits if the
primary case is fast.

Let's think about the information our caches give us right now:

* how many hidden classes seen (1, 2 to K, or &gt;K)
* which hidden classes seen (as long as &lt;= K)
* in what order the hidden classes were seen

But we want more information than that: we want to know if the access patterns
are skewed in some way.

## ClassDistribution

## ClassDistributionSummary

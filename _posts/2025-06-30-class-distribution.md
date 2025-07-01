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
become an integer comparison and a memory load.

Hidden classes give you the ability to more quickly read from objects, but you,
the runtime implementor, have to decide what kind of cache you want to use.
Should you have a monomorphic cache? Or a polymorphic cache?

In an interpreter, a common approach is to do some kind of [state-machine-based
bytecode rewriting](/blog/inline-caching-quickening/). Your generic opcodes
(load an attribute, load a method, add) start off unspecialized, specialize to
monomorphic when they first observe a hidden class HC, rewrite themselves to
polymorphic when they observe the next hidden class HC', and may again rewrite
themselves to megamorphic (the sad case) when they see the K+1th hidden class.
Pure interpreters take this approach because they want to optimize as they go.

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



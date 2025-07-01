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
become an integer comparison and a memory load. They rely on [the
assumption][smalltalk] that even in a dynamic language, programmers are not
very creative, and therefore for a given location in the code (PC), the number
of types seen will be 1 or small.

[smalltalk]: https://dl.acm.org/doi/pdf/10.1145/800017.800542

> See a [great
> tutorial](https://aosabook.org/en/500L/a-simple-object-model.html) by CF
> Bolz-Tereick on how to build a hidden class based object model.

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
* if polymorphic, in what order the hidden classes were seen

But we want more information than that: we want to know if the access patterns
are skewed in some way.

What if at some PC the interpreter sees 100x hidden class A and only 2x hidden
class B? This would unfortunately look like a boring polymorphic `[A, B]`
cache.

Or, maybe more interesting, what if we have a megamorphic site *but* one class
more or less dominates? This would unfortunately look like a total bummer case
even though it might be salvageable.

If only we had a nice data structure for this...

## ClassDistribution

S6 has this [small C++ class][ClassDistribution-h] called `ClassDistribution`
that the interpreter uses to register what hidden classes it sees during
execution profiling. It dispenses with the implicit seen order that a polymorphic
cache keeps in its cmp-jcc chain and instead uses two fixed-size (they chose
K=4) parallel arrays: `bucket_class_ids_` and `bucket_counts_`.

[ClassDistribution-h]: https://github.com/google-deepmind/s6/blob/69cac9c981fbd3217ed117c3898382cfe094efc0/src/type_feedback.h#L34

Every time the interpreter captures a profile, it calls
[`ClassDistribution::Add`][ClassDistribution::Add], which increments the
corresponding count associated with that ID. There are a couple of interesting
things that this function does:

[ClassDistribution::Add]: https://github.com/google-deepmind/s6/blob/69cac9c981fbd3217ed117c3898382cfe094efc0/src/type_feedback.cc#L28

1. Bubble the most frequently occurring hidden class's bucket to slot 0. It's
   not a full sort, but they say it helps optimize `Add` and makes
   summarization easier (more on that later)
1. If there are more than K classes observed, increment another field called
   `other_count_` to track more information about how megamorphic the call-site
   is
1. Keep a running tally of the difference between the sum total of the K
   buckets and the `other_count_` using a field called `count_disparity_`. If
   this gets too high, it indicates that the execution patterns have shifted
   over time and that it might be time to reset the stats
1. If they reset the stats, they keep track of the total count of events that
   happened before the reset in a field called `pre_reset_event_count_`. This
   can be used to determine if the current epoch has seen a statistically
   sigificant number of events to the pre-reset epoch

## ClassDistributionSummary

## See also

FeedbackVector

What if we had more context? Info from caller

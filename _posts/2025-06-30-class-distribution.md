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

> One interesting observation here is that while the bytecoder rewriting is
> used to help interpreter performance, you can reuse this specialized bytecode
> and its cache contents as a source of profiling information when the JIT
> kicks in. It's a double use, which is a win for storage and run-time
> overhead.

In an optimizing JIT world that cares a little less about interpreter/baseline
compiler performance, the monomorphic/polymorphic split may look a little
different:

1. monomorphic: generating code with a fixed hidden class ID to compare against
   and a fixed field offset to load from, and jumping into the interpreter if
   that very specific assumption is false
2. polymorphic: a self-modifying chain of such compare+conditional jump+load
   sequences, usually ending after some fixed number K entries with a jump into
   the interpreter

If you go for monomorphic and that code never sees any other hidden class,
you've won big: the generated code is small and generally you can use these
very strong type assumptions from having burned it into the code from the
beginning. If you're wrong, though, and the that ends up being a polymorphic
site in the code, you lose on performance: it will be constantly jumping into
the interpreter.

If you go for polymorphic but the code is mostly monomorphic TODO

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

That is not much more additional space and it gets you a totally different
slice of the picture than a "normal" IC and bytecode rewriting. I find the
bubbling up, the other count, and the running difference especially fun.

After a while, some bit of policy code decides that it's time to switch
execution modes for a given function and compile. The compiler would like to
make use of this profile information. Sure, it can fiddle around with it in its
raw state, but the S6 devs found a better API that random compiler passes can
consume: the `ClassDistributionSummary`.

## ClassDistributionSummary

The [`ClassDistributionSummary`][ClassDistributionSummary-h] is another very
small C++ class. It has only three fields: the class IDs from the
`ClassDistribution` (but *not* their counts), a `kind_` field, and a `stable_`
field.

[ClassDistributionSummary-h]: https://github.com/google-deepmind/s6/blob/69cac9c981fbd3217ed117c3898382cfe094efc0/src/type_feedback.h#L128

We don't need their counts because that's not really the question the optimizer
should be asking. The thing the optimizer *actually* wants to know is "how
should I speculate at this PC?" and it can outsource the mechanism for that to
the `ClassDistributionSummary`'s *kind* (and the information implicit in the
ordering of the class IDs, where the hottest class ID is in index 0).

The *kind* can be one of five options: *Empty*, *Monomorphic*, *Polymorphic*,
*SkewedMegamorphic*, and *Megamorphic*, each of which imply different things
about how to speculate. Empty, monomorphic and polymorphic are reasonably
straightforward (did we see 0, 1, or <= K class IDs?) but SkewedMegamorphic is
where it gets interesting.

Their heuristic for if a megamorphic PC is skewed is if the class ID in bucket
0---the most popular class ID---is over 75% of the total recorded events. This
means that the optimizer still has a shot at doing something interesting at the
given PC.

I wonder why they didn't also have SkewedPolymorphic. I think that's because
for polymorphic PCs, they inline the entire compare-jump chain eagerly, which
puts the check for the most popular ID in the first position. Still, I think
there is potentially room to decide to monomorphize a polymorphic call site.
There's some ad-hoc checking for this kind of thing in `optimize_calls.cc`, for
example to specialize `a[b]` where `a` is historically either a `list` or a
`tuple`.

Also, sadly, they did not get to implemented SkewedMegamorphic before the
project shut down, so they only handle monomorphic and polymorphic cases all
across the optimizer. Ah well.

## See also

FeedbackVector in V8. See [blog post by Benedikt
Meurer](https://benediktmeurer.de/2017/12/13/an-introduction-to-speculative-optimization-in-v8/),
which explains how they profile generic instruction operands using a feedback
lattice.

[Speculation in
JavaScriptCore](https://webkit.org/blog/10308/speculation-in-javascriptcore/),
which continues to be a fantastic resource for fast runtime development. In it,
Fil argues that the cost of speculating wrong is so high that you better be
darn sure that `cond` is true in `if (!cond) { side_exit(); }`

See a [blog post by Jan de Mooij](https://jandemooij.nl/blog/cacheir/) and a
[blog post by Matthew
Gaudet](https://www.mgaudet.ca/technical/2023/10/16/cacheir-the-benefits-of-a-structured-representation-for-inline-caches)
on CacheIR in SpiderMonkey (and [paper!](/assets/img/cacheir.pdf) (PDF))

&rarr; helpful for trial inlining? See [warp improvement blog post](https://hacks.mozilla.org/2020/11/warp-improved-js-performance-in-firefox-83/)

Tracing is just different

Basic block versioning

What if we had more context? Info from caller

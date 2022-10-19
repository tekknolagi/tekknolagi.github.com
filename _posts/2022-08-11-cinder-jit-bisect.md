---
title: "How we use binary search to find compiler bugs"
layout: post
date: 2022-08-11
description: >
  Inlining is one of the most important compiler optimizations. This post
  describes the Cinder JIT's function inliner and how it speeds up Python code.
---

I work on [Cinder](https://github.com/facebookincubator/cinder), a just-in-time
(JIT) compiler built on top of CPython. This post will talk about how we use
binary search to track down compiler bugs, but this is a technique that is
applicable to any compiler if you have the right infrastructure.

## Motivation

Sometimes---frequently---I change an optimization pass or a code generation
step and I break something. In the best case scenario, I end up with a failing
test and the test name or body give me enough clues to fix my silly little
mistake.  But in the worst case I can't even boot to the Python prompt because
I messed up something so badly. This can manifest as an exception, a failing
`assert`, or even a segmentation fault.

Since the runtime could have compiled any number of functions in that boot
process or test run, there are a lot of moving parts. I generally don't want to
look at the source, intermediate representations, and assembly of 1000
different Python functions. That's too much. It would be nice to have one to
two functions to look at and make inferences. This is where bisect comes in.

## Bisect

You may have heard of bisecting from geometry or from [`git
bisect`][git-bisect][^git-bisect]. Those are the two places I heard about it.
It means *to cut in half*. In our case and the Git case, not just once---many
times!

[git-bisect]: [https://git-scm.com/docs/git-bisect

[^git-bisect]: It's a super helpful tool and I highly recommend getting
    familiar with it. It has saved me hours in figuring out what commit caused
    problems.

We want to take our list of compiled functions and continuously cut it in half
until we reach a very small group of functions that cause us trouble.

Say we run `./python -X jit some_program.py` and it crashes. In the course of
this run it compiles functions `A`, `B`, `C`, and `D`, runs some code, and
aborts. We can't know which function is miscompiled, so we will try and bisect:

* Try `A` and `B` together. Runs.
* Try `C` and `D` together. Crash.
* Try `C`. Runs.
* Try `D`. Crash.

Since we can discard half of this set each time, we can find our miscompiled
functions in logarithmic time[^algo]. That's awesome.

[^algo]: This is one of the few regressions that has stuck with me from my
    undergraduate algorithms course.

## Requirements

While it's super helpful in many cases, bisecting has some prerequisites before
it can work well:

* A consistent reproducer. If your program non-deterministically fails, the
  bisect results won't make any sense.
* The ability to set the list of functions to compile. This requires some
  cooperation from the runtime.
* The ability to figure out which functions have been compiled. This also
  requires some cooperation from the runtime.

## Implementation details



[jitlist_bisect.py]: https://github.com/facebookincubator/cinder/blob/b1c65a7c3cd557854299d5c66bbfe6de1f4ed49d/Tools/scripts/jitlist_bisect.py


## Similar work

* Creduce


<hr style="width: 100px;" />
<!-- Footnotes -->

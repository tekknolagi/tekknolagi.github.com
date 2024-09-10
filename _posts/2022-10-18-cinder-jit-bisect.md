---
title: "How we use binary search to find compiler bugs"
layout: post
date: 2022-10-18
description: >
  Bisecting helps compiler developers find and fix their bugs.
---

I work on [Cinder](https://github.com/facebookincubator/cinder), a just-in-time
(JIT) compiler built on top of CPython. If you aren't familiar with Cinder and
want to learn more, [a previous post about the
inliner](/blog/cinder-jit-inliner/) gives a decent overview of the JIT. This
post will talk about how we use binary search to isolate miscompiled functions,
a technique that is applicable to any compiler if you have the right
infrastructure.

## Motivation

Sometimes---frequently---I change an optimization pass or a code generation
step and I break something. In the best case scenario, I end up with a failing
test and the test name or body gives me enough clues to fix my silly little
mistake.  But in the worst case I can't even boot to the Python prompt because
I messed something up so badly. This can manifest as an exception, a failing
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

[git-bisect]: https://git-scm.com/docs/git-bisect

[^git-bisect]: It's a super helpful tool and I highly recommend getting
    familiar with it. It has saved me hours in figuring out what commit caused
    problems.

We want to take our list of compiled functions and continuously cut it in half
until we reach a very small group of functions that cause us trouble.

Say we run `./python -X jit some_program.py` and it crashes. In the course of
this run, the JIT compiles functions `A`, `B`, `C`, and `D`, runs some code,
and aborts. We can't know which function is miscompiled, so we will try and
bisect:

* Try `A` and `B` together. Success.
* Try `C` and `D` together. Crash.
* Try `C`. Success.
* Try `D`. Crash.

Looks like `D` is the troublemaker.

Since we can discard half of this set each time, we can find our miscompiled
functions in logarithmic time[^algo]. That's awesome. Even on slow-running
repros, this rarely takes significant time. At worst, I go make tea.

[^algo]: This is one of the few regressions that has stuck with me from my
    undergraduate algorithms course.

## Requirements

While it's excellent in many cases, bisecting has some prerequisites:

* A consistent reproducer. If your program non-deterministically fails, the
  bisect results won't make any sense.
* Stable enough identifiers for functions. In this case we use the module name
  and fully qualified function name.
* The ability to set the list of functions to compile. This requires some
  cooperation from the runtime.
* The ability to figure out which functions have been compiled. This also
  requires some cooperation from the runtime.

We already have the second two due to some server architecture constraints.

## Implementation details

Cinder can be run with `-X jit-list-file=somefile.txt` and will only compile
functions on the JIT list (but will still only compile on first run).

The bisect script is a wrapper like `./jitlist_bisect.py ./python -X jit ...`
which interprets the debug output to figure out which functions were compiled
and passes in the JIT list.

Sometimes a JIT list will cause the crash but each split half won't. In that
case we hold each half fixed and try bisecting the other half to figure out
what candidates we need. *Update:* I have been told that this is called delta
debugging.

The script is less than 200 lines of Python and can be found
[here][jitlist_bisect.py]. A typical run looks like this:

```
$ ./Tools/scripts/jitlist_bisect.py ./build/python -X jit -m unittest test.test_import
INFO:root:Generating initial jit-list
INFO:root:Verifying jit-list
INFO:root:step fixed[0] and jitlist[504]
INFO:root:504 candidates
INFO:root:252 candidates
INFO:root:126 candidates
INFO:root:63 candidates
INFO:root:32 candidates
INFO:root:16 candidates
INFO:root:8 candidates
INFO:root:4 candidates
INFO:root:2 candidates
Bisect finished with 1 functions in jitlist.txt
$ cat jitlist.txt
warnings:_add_filter
$
```

Time to go look at the `warnings` module.

[jitlist_bisect.py]: https://github.com/facebookincubator/cinder/blob/b1c65a7c3cd557854299d5c66bbfe6de1f4ed49d/Tools/scripts/jitlist_bisect.py

See the core of the implementation extracted into a copy/pastable
non-JIT-specific couple of functions
[here](https://github.com/tekknolagi/omegastar).

## Other thoughts

Manually or automatically slimming down your reproducing source code also helps
with this approach. It makes the repro runtime shorter and sometimes removes
other moving parts like needing to send network traffic or something. We can
probably use some form of tracing and bisect to automatically slim the repro.

Compiler and interpreter unit tests can be a pain to write but they have saved
me countless times over the past couple of years. Being able to isolate each
and every small optimizer change is so, so helpful.

## Similar work

### C-Reduce

Speaking of automatically slimming the repro, [C-Reduce][creduce] is a tool by
John Regehr and his collaborators. It takes a C source file and runner script
and automatically bisects it to some failing case. From their homepage:

[creduce]: https://embed.cs.utah.edu/creduce/

> C-Reduce is a tool that takes a large C, C++, or OpenCL file that has a
> property of interest (such as triggering a compiler bug) and automatically
> produces a much smaller C/C++ file that has the same property. It is intended
> for use by people who discover and report bugs in compilers and other tools
> that process source code.

Incidentally, at the time of writing, their website looks pretty similar to
this one.

### Python's test case bisect module

Victor Stinner wrote [`test.bisect_cmd`][test.bisect_cmd][^rename] to debug failing
tests in CPython. This is useful when they only fail in certain arrangements!
This tool takes a slightly different approach from *delta debugging* and
instead uses random sampling.

[test.bisect_cmd]: https://vstinner.github.io/python-test-bisect.html

[^rename]: Note that this has since been renamed from `test.bisect` to
    `test.bisect_cmd` to avoid conflicting with the built-in `bisect` module.

### Fuzzing

Hacker News user *eimrine* also notes that this is similar to
[fuzzing][fuzzing], a technique to find bugs by changing input data or input
programs.

[fuzzing]: https://en.wikipedia.org/wiki/Fuzzing

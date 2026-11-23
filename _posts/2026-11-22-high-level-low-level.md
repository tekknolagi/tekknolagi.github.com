---
title: "High-level low-level programming"
layout: post
---

After [Inside PyPy: first contact with
RPython](/blog/inside-pypy-first-contact-with-rpython/), I wanted to document a
pattern that I keep describing to people.

People implement a programming language "PL". They implement in a
language---say, C---but get fed up with dealing with the quirks and lack of
features. So they take some of the creature comforts of PL and implement
"Pre-PL", which is kind of like a smaller and lower-level version of PL that
compiles to native code. Then they implement PL in Pre-PL.

The first time I heard about this was technically PyPy's RPython, I think. But
it's not the first implementation of this idea. Here is a chronological list of
projects doing such a thing:

In 1987, Richard Kelsey and Jonathan Rees implement [*Scheme
48*](https://s48.org/) in a language called
[*Pre-Scheme*](https://prescheme.org/).

In 1997, Dan Ingalls, Ted Kaehler, John Maloney, Scott Wallace, and Alan Kay
implement [*Squeak*](https://squeak.org/) in a language called
[*Slang*](https://wiki.squeak.org/squeak/slang). See also the later
[*Lowtalk*](https://github.com/ronsaldo/lowtalk).

In 2005, the PyPy team manage [to bootstrap
PyPy](https://pypy.org/posts/2018/09/the-first-15-years-of-pypy-3412615975376972020.html#bootstrapping-pypy)
with their new RPython compiler. They cite Squeak's Slang and also make perhaps
the first ever six-seven joke.

In 2009, the [Demystifying
magic](https://dl.acm.org/doi/10.1145/1508293.1508305) paper is published at
TODO and describes the techniques in DRLVM and [*Jalape&ntilde;o/Jikes
RVM*](https://www.jikesrvm.org/).

<!--
* Ruby builtins, kinda (cite TruffleRuby, maybe JRuby)
-->

Related:

* V8 Torque
* JSC DOMJIT

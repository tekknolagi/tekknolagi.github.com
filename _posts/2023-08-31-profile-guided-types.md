---
title: "Conjuring types from the void"
layout: post
description: Where do types come from?
---

You're frowning, you realize. You're still frustrated by that internet comment
someone with a randomized username made the other day. You log onto the forum
to write an impassioned response but realize that you're probably also
dehydrated, and drinking water might make you feel better. You drink. It does.

The commenter asserted that your project is a little bit slow to execute, and
they're right. But they didn't stop there; the commenter continued and
critiqued your choice in programming language.

They might have been superficially right in that rewriting your side project in
the hot new quasi-functional statically-typed language would make it faster,
sure. But this argument feels morally wrong.

First, you have not fully squeezed every last drop of metaphorical juice from
your current implementation. There are probably twenty structural things you
can fix first.

Second, it doesn't have to be this way! You shouldn't have to completely
restart your project like that; your language runtime should give you a path
out of your prototype stage and into a more optimized "deployment" stage to
help you speed things up.

> does this wade too far into the language war?

I think this is possible, but there are a lot of questions to answer. Questions
such as, "Max, that was super vague. What on Earth are you talking about?"

## Taking a step back

Yeah, alright, I was talking about writing code in Python and other dynamic
languages. A common project lifecycle I see in the Python[^other-langs]
ecosystem is:

[^other-langs]: It's not specific to Python. It happens with Ruby and
    JavaScript too. Heck, people are even writing new Python and JS tooling in
    Rust.

* Write a version of the code in Python
* Grow it
* Become concerned about performance
* Try PyPy[^pypy] (maybe stop here)
* Rewrite it in some other language with zero-cost abstractions or whatever

[^pypy]: PyPy is awesome. No buts. It's not a panacea, though, and what I am
    proposing wouldn't be either. And sometimes you really do just want to do
    the type-driven compilation for reliable performance.

I think the last step shouldn't need to exist. I think instead you and the
language runtime have an ongoing dialogue:

* The runtime tells you what parts of your code are hot, what parts are mucking
  up performance, and maybe how to optimize them
* You modify the code
* Repeat

Then you, over time, understand more about the runtime's performance
characteristics and how you can shape your code[^gross-c] to take advantage of
them.

[^gross-c]: I super don't mean micro-optimizing and uglifying your code and
    adding inline assembly and all that. You can think of the process as
    "adding types", but it goes a little further than that.

A common question I get now is "why not just do all this silently inside a
just-in-time compiler?"---great question.

* JITs require warmup time
* JITs require explicit checks and additional memory allocation in order to
  validate fast-path assumptions
* JITs often require (significant) inlining to elide checks and do not often do
  interprocedural analysis
* It is hard to build a mental performance model even if you are an engineer
  working on the JIT
* Sometimes the language semantics actively prohibit some kinds of
  optimizations, but the programmer is not aware that they are (accidentally)
  requesting The Slow Thing instead of something faster and equivalent for
  their purposes

That's not to say that JITs are bad---I have worked on several; they are
not---but they are not a perfect fit for every situation.

Assuming you're either on board or otherwise curious, I'll paint you a little
picture.

## A little picture

It's not so long from now in the future. You're in charge of maintaining a
codebase that is a couple of millions of lines of code. At that size,
it becomes intractable to understand how all of the different component parts
interact---especially when people are relentlessly committing to it day after
day. The code shifts under you like sand.

Part of your job involves keeping performance reasonable, even as the features
pile up. It's time to figure out what's slow and why, and you have long since
lost an intuition for what might be the problem. But you have a friend, and
your friend might know the answer. Your friend is the language runtime. The
runtime is in charge of executing the code, after all. Why shouldn't it know
which parts are unnecessarily slow?

You pull up the diagnostics tab for your fleet of servers and sure enough,
there are some metaphorical angry blinking red lights.

The first light indicates that some function is allocating a lot of memory,
and, furthermore, this memory is very short-lived. A closer inspection reveals
that the function is using string concatenation instead of a string buffer API.

The second light indicates that another function is very big and therefore the
JIT is refusing to compile it. But it is very hot, so you would really like it
compiled. You look more closely and notice that it is a ten thousand line match
expression generated from your protobuf/thrift/... binding generator when it
could be a static lookup table. You go fix the binding generator.

The third light lets you know of a memory leak caused, indirectly/transitively,
by an ad-hoc application-level cache. The cache keeps some closures alive which
keep some call stacks alive which keep a bunch of big strings alive which ...
so you do something to fix the cache.

There are some other signals, not angry blinking lights, below the performance
warnings. They are observations and suggestions for improvement.

## Suggestions from the runtime

The runtime's profiler[^profiler] has observed that, whenever it samples the stack,
this very hot untyped function over here is passed an integer. The JIT has
prepared an optimized version for the integer path but it still has to keep the
checks in case this ever changes. If you add a type annotation to the function,
it can get checked in CI and then the compiler need not check anything at
run-time[^type-language].

[^profiler]: This kind of thing exists in the form of
    [MonkeyType](https://github.com/Instagram/MonkeyType) and
    [PyAnnotate](https://github.com/dropbox/pyannotate) and
    [RightTyper](https://github.com/RightTyper/RightTyper) and ...

[^type-language]: Unfortunately, many type languages [are not sufficiently
    expressive](/blog/typed-python/) to encode the checks that you would want
    for this situation. It's very possible to *add* this to the type language
    but it's not already present in the well-known and well-used type languages
    such as Python's PEP 484. The Cinder project's Static Python has added some
    of these richer types and checks.

Even better, in this other function, it has observed that the integer is only
ever in the range of 0-255. If you narrow the range on the type to an
`uint8_t`, the compiler can even unbox the number and avoid a bunch of boxing
(tagged pointer or heap-allocated integer) nonsense.

is letting you know that the runtime thinks some code
is dead. It has not been run in over a year (precluding rare stuff like tax
season code) but it keeps getting loaded at application boot and is taking up
hundreds of megabytes of heap space. Deleting this code reduces heap pressure,
eliminating major GC cycles and also letting you process more web requests
concurrently.

These are not limited to semantically-identical changes, either. One might
imagine a situation where the runtime suggests a different object
representation based on use. Using a full hash-based set but only storing and
checking small integers? Maybe use a bitset. Using an long array as a set?
Maybe consider a set. Things like that. I don't know why my ideas are all
set-related.

Perhaps you are calling a function F from two different unrelated functions A
and B. Function A sends it integers and function B sends it floats. For
whatever reason, F can't be inlined into each of A and B so F ends up
containing code for both integers and floats. Bummer! Perhaps the runtime could
suggest an annotation such as "monomorphize this function based on argument
types" (as demonstrated in the [RPython
post](/blog/inside-pypy-first-contact-with-rpython/)).

You can see this kind of thing in the evolution of the Java language. In order
to avoid heap allocation and a bunch of other associated overhead, Java
introduced [value
objects](https://openjdk.org/projects/valhalla/value-objects).

## Questions to answer

## A gradual migration

## Native extensions for performance

## One-shot rewriting everything to Rust

## Prior art

Typed Racket
Static Python (IG)
mypyc (black)
Julia

## Blah

Right now, these signals are generally surfaced some oblique way to the people
who develop and deploy the JIT and its runtime. I used to have a little
dashboard that I maintained that tracked a SQL view of certain slow signals in
the Cinder JIT. Every so often I would look at the table, sigh, and go fix the
slow part of the application. But why was I, the compiler developer, the only
one getting these signals? Part of it is because there is some amount of
"reading tea leaves" that is necessary to determine where and how the fix
should be made, but part of it is just laziness! This stuff should absolutely
be made available to anyone who wants to look.

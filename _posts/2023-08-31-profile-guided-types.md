---
title: "Conjuring types from the void"
layout: post
description: Where do types come from?
date: 2023-08-31
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

That's not to say that JITs are bad---I have worked on several; they are
not---but they are not a perfect fit for every situation.

Assuming you're either on board or otherwise curious, I'll paint you a little
picture.

## A little picture

## Questions to answer

## Prior art

Typed Racket
Static Python (IG)
mypyc (black)
Julia

<br />
<hr style="width: 100px;" />
<!-- Footnotes -->

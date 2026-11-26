---
title: "In praise of lens grinding"
layout: post
---

I like to tell this story of an Old Scientist whose students were learning to
make their own lenses. Maybe for microscopes, or for telescopes... I don't
remember, exactly. They wanted to see something close. Or maybe far.

The Scientist guides the students to focus on quantity: to make as many lenses
as they possibly could. Only then would they be able to make good lenses.
Something something something and the students were enlightened.

It's a very fuzzy (and in this post, uninspiring) koan but the point I am
trying to make is that frequent, iterative practice and starting repeatedly
from scratch are super important for learning.

Only now, when trying to find this fable, did I learn that it's from *Art &amp;
fear* by David Bayles and Ted Orland, and it's actually about
ceramics[^photography]. I have no earthly idea how I confused it with lens
grinding. Perhaps I was reading too much Neal Stephenson when I started talking
about this[^quicksilver].

[^photography]: [*Or is it*](https://austinkleon.com/2020/12/10/quantity-leads-to-quality-the-origin-of-a-parable/)?

[^quicksilver]: Somewhere in the absolute tome Quicksilver is some fanciful
    description of the Royal Society but I do not presently have the patience
    to flip through it and find what I am talking about.

...

In this post, I will tell you my experience of profligate metaphorical wasted
glass/clay and how that informs all of my programming.

## Small examples

I learned a lot about static analysis in the last 12 months. If you have been
even occasionally following my blog, you may have seen posts such as
[Type-based alias analysis in the Toy Optimizer](/blog/toy-tbaa/) and [Load
and store forwarding in the Toy Optimizer](/blog/toy-load-store/).

I have also done other small project such as [zero-knowledge
proofs](/blog/zkp/) and [a shape/inline cache
implementation](/blog/object-shapes/).

All of these implementations are under 200 lines of code. Many of them are
under 100 lines of code. They are either tiny components of a larger
system---an optimization pass, or a slice of an optimization pass, of a bigger
compiler under a microscope---or a simplified model of a problem and its
solution.

This lets you focus on the part you want to think about and understand rather
than every other inane detail. For example, I want to think about static
analysis. If I wanted to build a small static analysis engine for a small
programming language, I might also have to either:

* build all sorts of other things like a parser, a backend, a way to test it,
  or
* find an existing implementation to understand and modify

If I only do the simple model, I can focus on the details of the problem at
hand.

You also don't see the process. You only see the prose and the small code
snippets. You *don't* see the thirty-odd hours building and editing and leaving
it on my disk for months and stewing about it. But you *have* to throw your
code away.

## Throw it away

It's kind of like a backtracking search. If you start building/searching on the
wrong path, you might not find a good solution. So you have to get used to
extreme amounts of undo/redo or just starting afresh. This lets you explore a
lot of different paths.

There are a dozen ways I could have modeled some of the data structures for
these little projects. I tried a lot of them for each. Sometimes you can
immediately tell when it's not the right one but sometimes you need to discover
that feeling creeping in

## Write about it

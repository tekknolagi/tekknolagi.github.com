---
title: "Walking around the compiler"
layout: post
---

Walking around outside is good for you.<sup>[<a href="https://en.wikipedia.org/wiki/Wikipedia:Citation_needed"><i>citation needed</i></a>]</sup>
A nice amble through the trees can quiet inner turbulence and make complex
engineering problems disappear.

Vicki Boykis wrote a post, [Walking around the
app](https://vickiboykis.com/2025/09/09/walking-around-the-app/), about a more
proverbial stroll. In it, she talks about constantly using your production
application's interface to make sure the whole thing is cohesively designed
with few rough edges.

She also talks about walking around other parts of the *implementation* of the
application, fixing inconsistencies, complex machinery, and broken builds. Kind
of like picking up someone else's trash on your hike.

That's awesome and universally good advice for pretty much every software
project. It got me thinking about how I walk around the compiler.

## What does your output look like?

There's a certain class of software project that transforms data---compression
libraries, compilers, search engines---for which there's another layer of
"walking around" you can do. You have the code, yes, but you also have
*non-trivial output*.

<!-- TODO pick another term -->

By non-trivial, I mean an output that scales along some quality axis instead of
something semi-regular like a JSON response. For compression, it's size. For
compilers, it's generated code.

You probably already have some generated cases checked into your codebase as
tests. That's awesome. I think golden tests are fantastic for correctness and
for people to help understand. But this isolated understanding may not scale to
more complex examples.

How *does* your compiler handle, for example, switch-case statements in loops?
Does it do the jump threading you expect it to? Maybe you're sitting there idly
wondering while you eat a cookie, but maybe that thought would only have
occurred to you while you were scrolling through the optimizer.

<!-- TODO maybe pick another example -->

## Mechanical sympathy and the compiler explorer

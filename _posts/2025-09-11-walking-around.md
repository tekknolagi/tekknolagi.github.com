---
title: "Walking around the compiler"
layout: post
---

Walking around outside is good for you.<sup>[<a href="https://en.wikipedia.org/wiki/Wikipedia:Citation_needed"><i>citation needed</i></a>]</sup>
A nice amble through the trees can quiet inner turbulence and make complex
engineering problems disappear.

Vicki Boykis's wrote a post about a more proverbial stroll, [Walking around the
app](https://vickiboykis.com/2025/09/09/walking-around-the-app/), which talks
about constantly using your production application's interface to make sure the
whole thing is cohesively designed with few rough edges. In it she also talks
about walking around other parts of the *implementation* of the application,
fixing inconsistencies, complex machinery, and broken builds.

That's awesome and universally good advice for pretty much every software
project. It got me thinking about how I walk around the compiler.

There's a certain class of software project that transforms data---compression
libraries, compilers, search engines---for which there's another layer of
"walking around" you can do.

## What does your output look like

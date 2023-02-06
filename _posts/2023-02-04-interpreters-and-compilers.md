---
title: "Interpreters and compilers"
layout: post
date: 2023-02-04
---

## Intro

Compilers and interpreters are not as different as people often make them out
to be. I don't mean in the [Futamura projection][futamura] sense, where
everything is a specializer. I mean this more as an observation: compilers
often contain interpreters and interpreters often contain
compilers[^futamura-related].

[futamura]: https://en.wikipedia.org/wiki/Partial_evaluation#Futamura_projections

[^futamura-related]: Perhaps this co-occurrence is related to the Futamura
    projections. I haven't thought about that too much. But it's not the point
    of this particular text.

If you think about your favorite interpreters, you might notice that they often
have some middle stage that front-loads some of the interpretation work.
Sometimes this is does not exist at all: MRI (the main Ruby implementation)
used to interpret code right off the AST. Now, it's just completely invisible:
Ruby compiles ASTs to bytecode in memory and executes that. The change from AST
to bytecode was a big change in the amount of user program preprocessing, but
the only effect observable by Ruby programmers was a speed increase.

Good data structures are crucial for language implementation performance. The
change in representation from AST to bytecode may not seem like a big one, but
the transformation from a pointer-heavy tree data structure to a compact linear
structure gives significant wins on modern hardware. It's kind of like going
from iterating over a linked list to iterating over an array; machine caches
were built for arrays and as long as you iterate somewhat predictably, reading
in the next byte of data is very fast.

For whatever reason, people feel compelled to make a big hullabaloo about the
distinction between compilers and interpreters[^languages-implementations]. My
friend Kartik and I don't agree with this, even if the way he phrases it might
make it seem otherwise:

[^languages-implementations]: Not to mention conflating languages and
    implementations. But that's both an inference you can make from this post
    and a rant for another day.

> It would be cool to go from BF interpreter all the way to a real compiler.

We had a conversation about this after reading Laurie Tratt's Brainfuck
interpreter post, [*Compiled and Interpreted Languages: Two Ways of Saying
Tomato*][ltbf]. Kartik continues about what he wants to see:

[ltbf]: https://tratt.net/laurie/blog/2023/compiled_and_interpreted_languages_two_ways_of_saying_tomato.html

> In the beginning it runs the code with zero prep. At the end it does a lot of
> prep before running the code. In between it does some intermediate amount of
> data structure initialization.

Maybe just reading Laurie's post and this commentary gives you enough insight
about the nature of compilers and interpreters and you can close this tab,
satisfied. That would be totally great. If not, though, strap in. We're going
to do the whole enchilada. We're going to write a lot of different
interpreters. With each interpreter, we will identify a bottleneck in
interpretation and adjust both the data structures and the amount of
preprocessing to make that bottleneck go away. We will continue until we arrive
at a native code compiler. Then we will continue some more.

## A lay of the land

I looked at a couple of different small languages while trying to decide which
to use. I wanted something bigger than Brainfuck---inscrutable, too few
operations, not similar enough to other languages---but not big enough that
this work would take a lifetime. Ideally, we could even get it done in a couple
of months.

* Tiger
* MinCaml
* Decaf
* ChocoPy
* GoLite
* Xi
* Wabbit

After surveying the list, I landed on Wabbit. It's small, useful enough, and
does not include any features that might require significant unexpected design
work, like classes or concurrency. A close second was Tiger, since I am already
familiar with it, but I think it is needlessly big for this exercise. Extending
Wabbit into Tiger is left as an exercise for the reader.

<br />
<hr style="width: 100px;" />
<!-- Footnotes -->

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

If checking (and, later, testing) your assumptions is tricky, this may be a
sign that your library does not expose enough of its internal state to
developers. This may present a usability impediment that prevents you from
immediately checking your assumptions or suspicions.

<!-- TODO link to Kate -->

Even if it does provide a flag like `--zjit-dump-hir` to print to the console,
maybe this is hard to run from a phone[^log-off] or a friend's computer. For
that, you may want *friendlier tools*.

[^log-off]: Just make sure to log off and touch grass.

## Mechanical sympathy and the compiler explorer

The right kind of tool invites exploration.

Matthew Godbolt built the first friendly compiler explorer tool I used, the
Compiler Explorer ("Godbolt"). It allows inputting programs into your web
browser in many different languages and immediately seeing the compiled result.
It will even execute your programs, within reason.

This is a powerful tool:

1. The feedback is near-instant and live updates on key-up.
1. There is no fussing with the command line and file watching.
1. Where possible, it highlights slices of source and compiled result to
   indicate what regions produced what output.
1. It's open source and you can add your own compiler.

This combination lowers the barrier to check things *tremendously*.

Now, sometimes you want the reverse: a Compiler Explorer -like thing in your
terminal or editor so you don't have to break flow. I unfortunately have not
found a comparable tool.

In addition to the immediate effects of being able to spot-check certain inputs
and outputs, continued use of these tools builds long-term intuition about the
behavior of the compiler. It builds *mechanical sympathy*.

I haven't written a lot about mechanical sympathy other than my grad school
[statement of purpose](/assets/img/statement-of-purpose.pdf) (PDF) and a few
brief internet posts, so I will leave you with that for now.

## Every function is special

Your compiler likely compiles some applications and you can likely get access
to the IR for the functions in that application.

Scroll through every function's optimized IR. If there are too many, maybe the
top N functions' IRs. See what can be improved. Maybe you will see some
unexpected patterns. Even if you don't notice anything in May, that could shift
by August because of compiler advancements or a cool paper that you read in the
intervening months.

One time I found a bizarre reference counting bug that was causing
copy-on-write and potential memory issues by noticing that some objects that
should have been marked "immortal" in the IR were actually being refcounted.
The bug was not in the compiler, but far away in application setup code---and
yet it was visible in the IR.

## Love your tools

My conclusion is similar to Vicki's.

Put some love into your tools. Your colleagues will notice. Your users will
notice. It might even improve your mood.

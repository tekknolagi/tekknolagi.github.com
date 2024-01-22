---
title: "scrapscript.py"
layout: post
date: 2024-01-31
---

For a little while now, [Chris](https://www.chrisgregory.me/) and I have been
working on a little interpreter for the [scrapscript](https://scrapscript.org/)
programming language. Now we would like to share it with you.

## What is scrapscript?

Scrapscript is a small, pure, functional, content-addressible, network-first
<!-- TODO: hmm not sure I love this wording --> programming language. The
language was created by [Taylor Troesh](https://taylor.town/) and the main
implementation was created by me and Chris.

```
fact 5
. fact =
  | 0 -> 1
  | n -> n * fact (n - 1)
```

It's designed to allow creation of small, simply shareable programs.

<!-- TODO -->

I'm not going to fill out the [usual
checklist](https://www.mcmillen.dev/language_checklist.html)---that's not the
point of the post, and a lot of it is answered on the website. This post is
more about the implementation.

## A bit of history

In April of 2023, I saw scrapscript posted on Hacker News and sent it to Chris.
We send each other new programming languages and he's very into functional
programming, so I figured he would enjoy it. He did!

But we didn't see any links to download or browse an implementation, so we were
a little bummed. We love trying stuff out and getting a feel for how it works.
A month or two passed and there still was not an implementation, so we decided
to email Taylor and ask if we could help.

Taylor was very gracious about the whole thing and shared his [small JavaScript
implementation of
scrapscript](https://github.com/tekknolagi/scrapscript/blob/71d1afecc32879aed9c80a3ed17cb81fe1c010d6/scrapscript.ts).
For such a little file, it implemented an impressive featureset.

Chris nor I are particularly good at JavaScript and we figured more
implementations couldn't hurt, so we decided to make a parallel implementation
with the same features. While Taylor's primary design constraint was
implementation size, ours was some combination of readability and correctness.
This meant that we wrote a lot of tests along the way to pin down the expected
behavior.

Two days into this little hackathon, we told Taylor and he was pretty happy
about it, so we started having semi-regular chats and continued hacking. Three
weeks later, we opened up [the repo](https://github.com/tekknolagi/scrapscript)
to the public. Please take a look around! Most of it is in one file,
`scrapscript.py`. This was one of our early design decisions...

## Implementation design decisions

While we weren't explicitly trying to keep the implementation size down like
Taylor was, we did want to keep it self-contained. That led to a couple of
implicit and explicit design choices:

**No external dependencies for core features.** Keep the core understandable
without needing to go off and refer to some other library.

As a sort of corollary, **try to limit dependencies on unusual or fancy
features of the host programming language (Python).** We wanted to make sure
that porting the implementation to another programming language was as easy as
possible.

**Test features at the implementation level and end-to-end.** Write functions
that call `tokenize` and `parse` and `eval_exp` explicitly so that we catch
errors and other behavior as close to the implementation as possible. Also
write full integration tests, because those can be used as a feature showcase
and are easily portable to other implementations.

Choosing Python was mostly a quirk. Python is not in general special; it just
happens to be a language that Chris and I have worked with a lot.

### Consequences of our testing strategy

Making sure to test early and test thoroughly had some excellent consequences.
It meant that we could keep a record of the expected behavior we discovered in
`scrapscript.js` as we ported it and have that record continuously checked. It
also meant that as we [gutted and re-built
it](https://github.com/tekknolagi/scrapscript/commit/082e30375225394f30fd270ffdcee7f5d63173ae),
we felt very confident that we weren't breaking everything.

## Why is this interpreter different from all other interpreters?

It's not. It's a pretty bog-standard tree-walking interpreter for a juiced-up
lambda calculus. Perhaps we'll eventually generate bytecode or some other IR
and compile it, but we do not have any performance problems (yet). And
scrapscript doesn't feel like an "industrial strength" language; nobody is
writing large applications in it and the language is expressly not designed for
that.

It's a little different *for me*, though, because it has features that I have
never implemented before! In particular, scrapscript supports some pretty
extensive pattern matching, which we had to learn how to implement from
scratch.

It's also different because it's the first from-scratch language implementation
I have worked on with someone else (I think). Chris has been an excellent
co-implementor, which is very impressive considering it his first programming
language implementation *ever*!

## Some neat implementation features

* REPL with readline
* Web REPL
* Cosmopolitan

- Impl language design decisions
  - What is different from other interpreters/compilers I’ve built
  - What was it like teaching someone who is not in PL about building interpreters/compilers (Chris)
    - C: Fun recap of COMP 105 but also brand new. Having second shot at PL
      after not doing so well in 105 was neat. Cool doing something
      collaborative and in the open. Interesting to have a foil / someone who
      knows about PL. How to work on project without fighting
    - M: Historically not worked together on larger projects but smaller ones
      went ok. What is making this one different? We’ll never know
      - kshake
      - 100prisoners
  - A little lambda calculus (See church encodings)
- Neat things
  - Web REPL
- Things that are broken
  - Serialization (recursion/closures)
- Things in progress
  - Scrapyard using Git
  - Graphics API
  - Platforms
  - Webserver platform with database
  - Alternates
- Call to action
  - Try out the web REPL

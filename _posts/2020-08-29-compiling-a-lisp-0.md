---
title: "Compiling a Lisp: Overture"
layout: post
date: 2020-08-29 13:16:00 PDT
description: Overture to our small Lisp to x86-64 compiler
og_image: /assets/img/compiling-a-lisp-og-image.png
series: compiling-a-lisp
---

<div style="width: 80%; margin: 0 auto;"><i>Many thanks to Kartik Agaram and
Leonard Sch&uuml;tz for proofreading these posts.</i></div>

In my [last series](/blog/lisp/), I wrote about building a
Lisp interpreter. This time, we're going to write a Lisp compiler.

This series is an adaptation of Abdulaziz Ghuloum's excellent paper [An
Incremental Approach to Compiler Construction](/assets/img/11-ghuloum.pdf),
with several key differences:

* Our implementation is in C, instead of Scheme
* Our implementation generates machine code directly, instead of generating
  text assembly
* Our implementation may omit some runtime data structures

See [my implementation](https://github.com/tekknolagi/ghuloum) for reference,
but note that it may be incomplete and also may look a little bit different
than the compiler detailed in these posts.

You probably have a couple questions, like *why Lisp?* and *why compile
directly to x86-64?* and *why C?* and *come on, another Lisp series?*, and
those are all very reasonable questions that will be answered in due time.

I want to implement this compiler in another language than Scheme because it
will prevent me from copying too much of the code from the paper. Even though
the paper doesn't actually contain the source for the whole compiler (most of
it is, after all, left as exercises for the reader), I think I will learn a lot
more when I have to write all of the code by myself. I get to make my own
mistakes and you get to watch me make and fix them in "real" time.

I also don't want to generate text assembly, but those reasons are a little
different than my reason for choosing another implementation language:

First, I think that would be harder to test: I want to have an in-process unit
test suite that compiles Lisp programs and executes them on-the-fly. Shelling
out to a system assembler like `as` or `nasm` would be somewhat error prone.
What if the person building this doesn't have the assembler I need? Sure, I
could also write a small assembler as part of this compiler, but that's a lot
of work.  Harder than just generating x86-64 directly, perhaps.

Second, I want to learn more about machine architecture. While `add a, b` seems
like one machine instruction, it could probably be encoded in 50 different ways
depending on whether `a` and `b` are registers, stack locations, other memory
addresses, immediates, which registers they are, etc. Shelling out to an
assembler abstracts a lot of that detail away. I *want* to get my hands dirty.
Hopefully you do, too.

I chose Lisp because that's what the Ghuloum paper uses, and because Lisp can
be represented as a small, compact, dynamically typed language. Many
interpreter implementations are under 200 lines. I don't think this compiler
will be that short, though.

For questions, comments, and suggestions please post on [this sr.ht
elist](https://lists.sr.ht/~max/compiling-lisp). It's a public inbox that I can
use to discuss and receive patches. I got the idea from [Chris
Wellons](https://nullprogram.com/).

### Background knowledge

In order to get the most out of this series, I recommend having at least passing
familiarity with the following:

* C or a C-like language
* some kind of assembly language
* *Abstract Syntax Trees* and recursive tree traversal
* no particular aversion to parentheses

Having the background helps your focus be more on the bigger picture than the
minutia, but it is by no means required. I expect most of this series to be
fairly readable. If it's not, that's a bug and you should report it to me.

### Structure of the series

I plan on writing this series in installments where each installment adds a
feature of some kind. Maybe that feature is a new bit of Lisp functionality, or
maybe it's a refactoring of the compiler, or maybe it's a compiler
optimization.

For this reason, each post will tend to depend on the code and understanding
from previous posts. As such, I recommend reading the series in order. I'll
still try to keep the big ideas understandable for those who don't.

At each stage of the compiler, we should have a battery of tests that ensure
that the features we have already build continue to work as expected.

I plan on adhering to this rough plan:

1. [Compile integers](/blog/compiling-a-lisp-2/)
1. [Compile other immediate constants](/blog/compiling-a-lisp-3/)
   (booleans, ASCII characters, the empty list)
1. [Compile unary primitives](/blog/compiling-a-lisp-4/)
   (`add1`, `sub1`, `integer->char`, `char->integer`, `null?`, `zero?`, etc)
1. [Compile binary primitives](/blog/compiling-a-lisp-5/)
   (`+`, `-`, `*`, `/`, `=`, etc)
1. [Read expressions from strings](/blog/compiling-a-lisp-6/)
1. [Compile local variables](/blog/compiling-a-lisp-7/)
   (`let`-expressions)
1. [Compile conditional expressions](/blog/compiling-a-lisp-8/)
   (`if`-expressions)
1. [Compile heap allocation](/blog/compiling-a-lisp-9/)
   (`cons`)
1. Compile heap allocation (strings, symbols, etc)
1. [Compile procedure calls](/blog/compiling-a-lisp-11/)
   (`labels`, `code`, and `labelcall`)
1. Compile closures
1. Add tail-call optimization
1. Compile complex constants (`quote`)
1. Compile variable assignment (`set!`)
1. Add macro expander
1. Add extended forms using macro expander (`let*`, `letrec`, etc)
1. Add support for libraries and separate compilation
1. Compile foreign function calls
1. Add error checking to primitives and procedure calls
1. Compile variable-arity procedures (aka varargs)
1. Compile `apply`
1. Add output ports (kind of like `FILE*`)
1. Add `write`, `display`
1. Add input ports
1. Add a tokenizer in Lisp
1. Add a reader in Lisp
1. Add a Lisp interpreter (or compiler) in Lisp

With optional add-ons also described in the initial paper:

* Big numbers, IEEE754 floats, complex numbers
* User-defined macros
* A module system
* Heap overflow handler and garbage collector
* Stack overflow handler
* Improved code generation

And optional add-ons not described in the original paper:

* An intermediate representation for optimization
* Generate executables and write them to disk
* An interpreter with optional just-in-time compiler

You may have noticed that this is a *lot* of steps, and there are some steps
that I intend to take but have completely omitted because I want to roll them
into other posts. Things like:

* Code generation infrastructure (a writable buffer, `mmap`/`mprotect`, etc)
* Compiler data structures (variable environments, label environments, etc)
* Testing infrastructure (unit testing, integration testing)

So it's really actually more work than I listed above. This series may take a
long time. It may take some twisty turns. It may take some shortcuts. But there
is good news: I've already written the compiler up to compiling heap allocation
(still working on procedure calls), and even if I don't finish this series
there is still Ghuloum's excellent paper to learn from.

Next up, [the smallest program](/blog/compiling-a-lisp-1/).

{% include compiling_a_lisp_toc.md %}

<hr style="width: 100px;" />

Here are some other links you might find useful or interesting while following
along with this series:

* Nada Amin's [implementation](https://github.com/namin/inc) of the original
  paper
* Rui Ueyama's Ghuloum-style [C compiler](https://github.com/rui314/chibicc)
* UMD's Ghuloum-style
  [compiler course](https://www.cs.umd.edu/class/fall2020/cmsc430/Notes.html)
  and associated
  [lectures](https://www.youtube.com/channel/UCfZ7HFoaeSA7zPoGelA3aiw/videos)

---
title: "Interpreters and compilers; or, Potato potato"
layout: post
date: 2023-06-30
---

## Intro

For whatever reason, people feel compelled to make a big hullabaloo about the
distinction between compilers and interpreters[^languages-implementations]. My
friend [Kartik](http://akkartik.name/) and I had a conversation about this
tendency after reading Laurence Tratt's Brainfuck interpreter post, [*Compiled
and Interpreted Languages: Two Ways of Saying Tomato*][ltbf].

[^languages-implementations]: Not to mention conflating languages and
    implementations. But that's both an inference you can make from this post
    and a rant for another day.

[ltbf]: https://tratt.net/laurie/blog/2023/compiled_and_interpreted_languages_two_ways_of_saying_tomato.html

Laurence's post does a great job of iteratively adding more ahead-of-time
preprocessing (compilation!) stages to a simple Brainfuck interpreter. If that
already makes you think enough about this, great. Feel free to close this tab.
If you want more, read on.

## Bigger languages

This post will show you a bunch of examples and ask you "interpreter or
compiler?" and hopefully you will realize the line is fuzzier than you
previously thought and maybe not always a useful distinction. Or at least that
if you're confused, it's a system that includes both an interpreter and a
compiler.

There are some projects like [Elk](https://github.com/cesanta/elk) that
run JavaScript right off the source text. I think most people would call this
an interpreter.

There are some languages like [Forth](https://en.wikipedia.org/wiki/Forth_(programming_language))
whose implementations generally read one word of input at a time and act on
that. Kind of like just running off of the output of a tokenizer. Interpreter?
Probably, yes.

Some projects like old versions of the main
[Ruby](https://github.com/ruby/ruby/) implementation (MRI) build an abstract
syntax tree (AST) and run code from the AST. Interpreter? Compiler? I think
most people would still say interpreter, despite having a transformation pass
from text to tree. If I recall correctly, people on the internet circa 2012
really loved to define "interpreter" as only the tree-walking kind.

Nowadays MRI turns the AST into bytecode before running it. This reduces the
amount of pointer chasing and allows for some optimization. This all happens
transparently to you, the programmer, and the bytecode never leaves the VM.
Hmmm, things are getting a little fuzzier. There's another code transformation,
this time from tree to linearized bytecode...

CPython takes this a step further with `.pyc` files and Java with `.class`
files (it's in the spec!). Does having an artifact on disk skew your perception
of what's going on? Even if the artifact is "just bytecode"?

Further, some Java runtimes like the [OpenJDK](https://github.com/openjdk/jdk)
even turn the bytecode into machine code before running it, though the machine
code tends not to hit the disk. At this point we're several transformation
layers deep. Is the OpenJDK an interpreter? A compiler?

And what about [Clang](https://github.com/llvm/llvm-project), which takes in
C++ code and emits machine code to disk? Would you call that a compiler? What
if I told you that they have not one, but two different interpreters inside the
compiler itself? That the constexpr tree-walking interpreter needed to be
turned into a bytecode interpreter to improve compile times?

...and the interpreter that runs your x86? Is it not an interpreter if it's
written in digital circuits?

I think the main takeaway I am trying to push is that internet discourse about
this has gotten a little silly and doesn't help people learn things.
Interpreters tend to contain compilers and compilers tend to contain
interpreters. It's a nice symbiotic coexistence. And if you really need to draw
a line somewhere, then I guess compilers turn programs into other programs and
interpreters turn programs into values. Or something like that. It gets a
little fuzzy when you treat the code as data.

## Epilogue

The post I was originally going to write on this topic involved actually
writing an interpreter and iteratively transforming it into more of a compiler,
doing all of the steps that the projects mentioned above do. But there are only
so many hours in the day, so this is left as an extended exercise for the
reader. Take a look at David Beazley's
[Wabbit](https://www.dabeaz.com/wabbit.html) as a good language target. Please
let me know if you do this and I will happily link it here.

Also, what this doesn't address is that some *languages* require more run-time
glue code, called "a runtime", to happen around the edges of your application
code. Features like reflection, dynamic dispatch, garbage collection, etc all
add a bit of runtime code into the mix. People who see implementations that
include a runtime tend to point their fingers and yell "interpreter!" but I
think it's a red herring.

Last, it doesn't address why you might want to do more or less program
transformation up front for your workload. Some very bright people at a large
social media company wanted to spin up a project for just-in-time (JIT)
compiling C++ because the cost of ahead-of-time (AOT) compiling a bazillion
lines of C++ was just too high. The same social media company, for a different
project, also wanted to spend a little *more* time compiling their Python
code to get better run-time performance.

So... find your place on the [Pareto
frontier](https://en.wikipedia.org/wiki/Pareto_front) and do as much
compilation as you need.

<br />
<hr style="width: 100px;" />
<!-- Footnotes -->

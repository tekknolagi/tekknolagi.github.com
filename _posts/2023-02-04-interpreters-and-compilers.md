---
title: "Interpreters and compilers; or, Potato potato"
layout: post
date: 2023-06-30
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

## blah

There's been a lot of back-and-forth about what it means to be an interpreter
and what it means to be a compiler. This post will show you a bunch of examples
and ask you "interpreter or compiler?" and hopefully you will realize the line
is fuzzier than you previously thought and maybe not always a useful
distinction.

There are some projects like [Elk](https://github.com/cesanta/elk) that
run JavaScript right off the source code. I think most people would call this
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

Nowadays MRI turns the AST into bytecode before running it. This all happens
transparently to you, the programmer, and the bytecode never leaves the VM.
Hmmm, things are getting a little fuzzier. There's another code transformation,
this time from tree to linearized bytecode...

CPython takes this a step further with `.pyc` files and Java with `.class`
files (it's in the spec!). Does having an artifact on disk skew your perception
of what's going on?

Further, some Java runtimes like the [OpenJDK](https://github.com/openjdk/jdk)
even turn the bytecode into machine code before running it, though the machine
code tends not to hit the disk. At this point we're several transformation
layers deep. Is the OpenJDK an interpreter? A compiler?

And what about [Clang](https://github.com/llvm/llvm-project), which takes in
C++ code and emits machine code to disk? Would you call that a compiler? What
if I told you that they have not one, but two different interpreters inside the
compiler itself? That the constexpr tree-walking interpreter needed to be
turned into a bytecode interpreter to improve compile times?

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
so many hours in the day. This is left as an extended exercise for the reader.
Take a look at David Beazley's [Wabbit](https://www.dabeaz.com/wabbit.html) as
a good language target.

Also, what this doesn't address is that some *languages* require more run-time
glue code, called "a runtime", to happen around the edges of your application
code. Features like reflection, dynamic dispatch, garbage collection, etc all
add a bit of runtime code into the mix. People who see implementations that
include a runtime tend to point their fingers and yell "interpreter!" but I
think it's a red herring.

Last, it doesn't address why you might want to do more or less program
transformation up front for your workload.

<!-- TODO -->

<br />
<hr style="width: 100px;" />
<!-- Footnotes -->

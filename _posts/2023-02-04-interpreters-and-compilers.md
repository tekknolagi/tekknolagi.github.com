---
title: "Interpreters and compilers"
layout: post
date: 2023-02-04
---

<!-- Should I call this post "potato potato" as a terrible joke reference to
Laurie's post? Or maybe I should call the interpreter potato and the compiler
potato, pronounced differently. -->

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
in the next byte of data is very fast[^interpretive-overhead].

[^interpretive-overhead]: Interpreter overhead and program optimization are
    similar but also different. Going from an AST to bytecode may not
    fundamentally alter the user program's meaning but the bytecode
    representation is more convenient to interpret. It's optimizing your
    program---the compiler author's program---as opposed to the user program.
    It's possible to do traditional compiler optimizations (strength reduction,
    etc) on either representation of the user program.

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
* Lisp

After surveying the list, I landed on Wabbit. It's small, useful enough, and
does not include any features that might require significant unexpected design
work, like classes or concurrency. A close second was Tiger, since I am already
familiar with it, but I think it is needlessly big for this exercise. Extending
Wabbit into Tiger is left as an exercise for the reader.

I also avoided Lisp because I want the broadest appeal possible. It's too easy
to write off a post using Lisp as its target language because it's "only
possible with Lisp" or "only possible for functional languages" or something
else. Also, I have too much Lisp content on this blog for someone who never
really writes Lisp.

[Wabbit][wabbit] is a small programming language created by [David
Beazley][dabeaz] to teach compilers. The following example snippet by David
gives a taste for the features we will need to implement. It looks a little bit
like Go:

[wabbit]: https://www.dabeaz.com/wabbit.html
[dabeaz]: https://www.dabeaz.com/index.html

```go
/* fib.wb -  Compute fibonacci numbers */

const LAST = 30;            // A constant declaration

// A function declaration
func fibonacci(n int) int {
   if n > 1 {              // Conditionals
       return fibonacci(n-1) + fibonacci(n-2);
   } else {
       return 1;
   }
}

func main() int {
   var n int = 0;            // Variable declaration
   while n < LAST {          // Looping (while)
       print fibonacci(n);   // Printing
       n = n + 1;            // Assignment
   }
   return 0;
}
```

Unlike many other tomes about compilers, we will not talk about lexing and
parsing here. We will instead start at the AST, assuming it has been neatly
generated for us by some parser. We skip this step because parsing is a
requirement for both interpreters and compilers, it distracts from the main
points of the post, is well-covered by other resources, and can be provided as
a little library for your perusal, if you are interested later on.

We will instead do the following:

* Write an AST interpreter
* Optimize the AST interpreter[^graal]
* Compile the AST to bytecode
* Write a bytecode interpreter
* Optimize the bytecode interpreter
* Compile the bytecode to machine code in memory
* Write the machine code to disk

[^graal]: Some interpreters don't make it past the AST interpreter stage
    because they don't need to! The authors of Graal, for example, have put in
    the work to make AST interpreters very very fast. This is an unusually
    impressive feat and we don't want to do that much work here.

At each stage, we will do a little more work up-front (before starting user
program execution) than before. This means that interpreter start-up will be
slower and user program execution will be faster[^user-needs].

[^user-needs]: Depending on the user program, this might be an acceptable
    trade-off. It might also not be, and you should stop earlier in the
    process. For example, programs like servers tend to be long-lived, so they
    want to optimize the user code as much as possible. Programs for scripting
    at the command line might value low start-up latency because they are
    interactive.

<br />
<hr style="width: 100px;" />
<!-- Footnotes -->

---
title: "Writing a Lisp, Part 0: Fundamentals"
author: Maxwell Bernstein
date: Oct 27, 2016
codelink: /resources/lisp/00_fundamentals.ml
layout: post
---

So you want to write a [Lisp
interpreter](https://en.wikipedia.org/wiki/Lisp_(programming_language)). I did
too, and then I wanted to write about it, so here we are with this post series.

I initially wanted to write a Lisp interpreter as an exercise. I decided to
write V1 in C (based on these two series of [blog](http://www.lwh.jp/lisp/)
[posts](http://peter.michaux.ca/articles/scheme-from-scratch-introduction)).[^other-resources]
After much pointer shenanigans and an unholy
amount of curly braces later, it worked &mdash; but was significantly longer
and harder to read than a Lisp interpreter should be. So I decided to write V2
in [OCaml](https://www.ocaml.org/), a functional programming language descended
from [SML](https://en.wikipedia.org/wiki/Standard_ML). The SML family of
languages has some features like pattern matching and an extensive type system
that make writing interpreters and compilers an absolute dream.

I'm going to walk through writing a fully-functional Lisp interpreter in OCaml.
If you're following along with the OCaml, great. If you want to try and build
it in SML or Haskell or whatever, it shouldn't be too hard to translate. If
you want to follow along in C... well, good luck. It'll be a lot more code.

I'm going to build up the interpreter in stages, starting with symbols and
other literals, then moving to simple math, then finally writing the features
to support a metacircular evaluator (meaning that you can write *another* Lisp
interpreter in the Lisp you just wrote!).

Let's begin!

We probably want our interaction with the interpreter to look something like
this:

```
$ ./repl
> 4
4
> ^D
$
```

So it seems reasonable to have our main file consist of a function that can
read an expression, evaluate it, print it, and continue. A [Read-Eval-Print-Loop
(REPL)](https://en.wikipedia.org/wiki/Read%E2%80%93eval%E2%80%93print_loop).

I found OCaml rather lacking in stream features, so I looked around the
internet and found this [lovely blog
post](http://troydm.github.io/blog/2014/03/29/writing-micro-compiler-in-ocaml/)
that has a file stream implementation. I decided to improve on that.

Our file stream feature will carry a line number, have a char buffer, and also
have an OCaml `in_channel`, which is more or less a glorified `FILE *`:

```ocaml
type stream =
  { mutable line_num: int; mutable chr: char list; chan: in_channel };;
```

Let's start with a function that can read in a character:

```ocaml
let read_char stm =
    match stm.chr with
      | [] ->
              let c = input_char stm.chan in
              if c = '\n' then let _ = stm.line_num <- stm.line_num + 1 in c
              else c
      | c::rest ->
              let _ = stm.chr <- rest in c
```

Let's take a look at that. There are two initial states that `stm` could be in
when we want to read a character &mdash; either it has characters in the
buffer, or it doesn't.

If there are no characters in the buffer, we should read one character from the
file stream. If it's a newline, we should increment the line number. If there
*are* characters in the buffer, we can pattern match against the list to get
the first character, then remove it from the buffer.

Neat! But now we should probably have our corresponding `unread_char` function
that we can use later for backtracking:

```ocaml
let unread_char stm c =
  stm.chr <- c :: stm.chr;;
```

This one will just concatenate the given character to the front of our char
buffer.

Next up, we should have a function that trims all the leading whitespace before
the text that we care about. We need this because Lisp is not
whitespace-sensitive.

```ocaml
let is_white c =
  c = ' ' || c = '\t' || c = '\n';;

let rec eat_whitespace stm =
  let c = read_char stm in
  if is_white c then
    eat_whitespace stm
  else
    unread_char stm c;
    ();;
```

This will read whitespace characters and ignore them until it hits a
non-whitespace character, at which point it will push it back on the buffer.
See, told you it would be useful.

One last thing: we'll want a type system to keep all of our different types of
[expressions](https://www.cs.sfu.ca/CourseCentral/310/pwfong/Lisp/1/tutorial1.html)
straight. Right now we only have numbers, but we'll add more later.

```ocaml
type lobject =
  | Fixnum of int
```

I think we're ready to get started! Let's read in a whole number:

```ocaml
let rec read_sexp stm =
  let is_digit c =
    let code = Char.code c in
    code >= Char.code('0') && code <= Char.code('9')
  in
  let rec read_fixnum acc =
    let nc = read_char stm in
    if is_digit nc
    then read_fixnum (acc ^ (Char.escaped nc))
    else
      let _ = unread_char stm nc in
      Fixnum(int_of_string acc)
  in
  eat_whitespace stm;
  let c = read_char stm in
  if is_digit c
  then read_fixnum (Char.escaped c)
  else raise (SyntaxError ("Unexpected char " ^ (Char.escaped c)));;
```

where `Char.escaped` turns a character into a string consisting of only that
character.

That was a lot at once. Let's break it down.

I defined several helper functions (`is_digit`, `read_fixnum`) *inside of*
`read_sexp` because they'll only really be useful there.

After the definitions we eat whitespace, then try and read in a number. If it
fails, we just raise a `SyntaxError`. Oh well!

Speaking of `SyntaxError`s, it's really easy to define your own
[exceptions](https://en.wikipedia.org/wiki/Exception_handling):

```ocaml
exception SyntaxError of string;;
```

For those unfamiliar with exceptions, they are a construct that changes the
flow of execution in a program. When `raise`d, they halt the program flow and
go "up the chain" in function calls until they find a place that specifically
looks out for them. If they don't find one (as in this case), OCaml just stops.

Let's give our program a whirl! Write a main function to be called on program
start, and have it call `read_sexp`:

```ocaml
let main =
  let stm = { chr=[]; line_num=1; chan=stdin } in
  print_string "> ";
  flush stdout;
  let Fixnum(v) = read_sexp stm in
  print_string "Your int: ";
  print_int v;
  print_newline ();;
```

You have to do this annoying thing `flush stdout` because otherwise the `> `
prompt will appear *after* you type your number in.

And let's run it!

```
$ ocaml 00_fundamentals.ml
> 4
Your int: 4
$
```

Cool! Looks like it works. But what happens if we try and enter a non-number?

```
$ ocaml 00_fundamentals.ml
> k
Exception: SyntaxError "Unexpected char k".
$
```

And what happens if we try and enter a negative number?

```
$ ocaml 00_fundamentals.ml
> -123
Exception: SyntaxError "Unexpected char -".
$
```

Well, we don't actually handle negative numbers right now. That's definitely
something that can be improved in the future.

Now let's make it a REPL! How do we loop in OCaml? Recursion, of course!

```ocaml
let rec repl stm =
  print_string "> ";
  flush stdout;
  let Fixnum(v) = read_sexp stm in
  print_int v;
  print_newline ();
  repl stm;;

let main =
  let stm = { chr=[]; line_num=1; chan=stdin } in
  repl stm;;
```

And that seems to work just fine:

```
$ ocaml 00_fundamentals.ml
> 4
4
> 5
5
> Exception: End_of_file.
$
```

It looks like OCaml's I/O functions raise the `End_of_file` exception if it
encounters EOF (aka me hitting `^D` or trying to pipe in a file), which we
could handle if we wanted. But I think this is fine for now.

Download the code <a href="{{ page.codelink }}">here</a> if you want to mess
with it.

Next up, [booleans](/blog/lisp/01_booleans/).

[^other-resources]:
    Here are some other resources or series you may find helpful in writing a
    Lisp interpreter.

    * [lwh.jp](https://www.lwh.jp/lisp/) (C)
    * [*Scheme from Scratch* by Peter Michaux](http://peter.michaux.ca/articles/scheme-from-scratch-introduction) (C)
    * [*Build Your Own Lisp* by Daniel Holden](http://buildyourownlisp.com/contents) (C)
    * [*Lisp interpreter in 90 lines of C++* by Anthony C. Hay](http://howtowriteaprogram.blogspot.com/2010/11/lisp-interpreter-in-90-lines-of-c.html)
    * [*mal* by kanaka](https://github.com/kanaka/mal/blob/master/process/guide.md)

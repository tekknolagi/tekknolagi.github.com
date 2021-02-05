---
title: "Writing a Lisp, Part 1: Booleans"
author: Maxwell Bernstein
date: Oct 28, 2016
codelink: /resources/lisp/01_booleans.ml
layout: post
---

Last time we wrote a simple interpreter that could read in numbers. That's cool
and all, but we'll need to read in more kinds of input to actually call this a
Lisp interpreter. Like booleans, for example! And while we're at it, we might
as well fix our situation with negative numbers.

We're going to want to input booleans like `#t` and `#f` for `true` and
`false`, respectively. This addition of the `#` does two things:

1. It makes clear to the reader that this is not a variable, but a fundamental
   constant
2. It makes writing our boolean reader easier

But I suppose we'll need to modify our reader to be able to handle booleans,
now, won't we. Why not just add a condition to check if the character `c` is a
`#`?

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
  if is_digit c then read_fixnum (Char.escaped c)
  else if c = '#' then
      match (read_char stm) with
      | 't' -> Boolean(true)
      | 'f' -> Boolean(false)
      | x -> raise (SyntaxError ("Invalid boolean literal " ^ (Char.escaped x)))
  else raise (SyntaxError ("Unexpected char " ^ (Char.escaped c)));;
```

Seems reasonable. Let's add the type constructor to our `lobject` so that this
all actually compiles:

```ocaml
type lobject =
  | Fixnum of int
  | Boolean of bool
```

The last thing we'll want to fix is the `repl` function. Right now it assumes
that `read_sexp` will always return a `Fixnum`, but that's not the case:

```ocaml
let rec repl stm =
  print_string "> ";
  flush stdout;
  let Fixnum(v) = read_sexp stm in
  print_int v;
  print_newline ();
  repl stm;;
```

I propose we make a separate function entirely for printing any type of
`lobject`:

```ocaml
let rec print_sexp e =
    match e with
    | Fixnum(v) -> print_int v
    | Boolean(b) -> print_string (if b then "#t" else "#f")

let rec repl stm =
  print_string "> ";
  flush stdout;
  let sexp = read_sexp stm in
  print_sexp sexp;
  print_newline ();
  repl stm;;
```

This also ends up making `repl` easier to read. Read. Print. Loop.

And now because I promised, I suppose we should make negative numbers work. To
do this we'll have to make sure that we check for a negative sign *or* a
digit:

```ocaml
  if (is_digit c) || (c = '~') then read_fixnum (Char.escaped (if c='~' then '-' else c))
  else if c = '#' then [...]
```

So we're not actually using the usual minus sign, `-`, for negative numbers.
We're using the tilde, SML-style, because it makes parsing symbols (next) just
a little bit easier. Here's what an interaction with our new REPL looks like:

```
$ ocaml 01_booleans.ml
> 14
14
> ~123
-123
> #t
#t
> #f
#f
> -
Exception: Failure "int_of_string".
$
```

At some point we should probably handle the `int_of_string` parse failure. But
it works!

Download the code <a href="{{ page.codelink }}">here</a> if you want to mess
with it.

Next up, [symbols](/blog/lisp/02_symbols/).

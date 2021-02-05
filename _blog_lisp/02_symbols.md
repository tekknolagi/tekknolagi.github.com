---
title: "Writing a Lisp, Part 2: Symbols"
author: Maxwell Bernstein
date: Nov 1, 2016
codelink: /resources/lisp/02_symbols.ml
layout: post
---

Last time we added booleans and negative numbers to our interpreter. But
booleans and negatives are kind of boring and we'll certainly need more
features for a full Lisp. So that brings us to
[symbols](http://stackoverflow.com/questions/8846628/what-exactly-is-a-symbol-in-lisp-scheme).

What is a symbol in Lisp? Well, it's anything that can be used as a name. So
things like `hello`, `+`, and `my-god-you've-gotten-fat` all work. Yes, with
the quote.

<img class="post-inline-image" src="/assets/img/lisp/edna.gif" />

I suppose we should add a new type constructor to `lobject`:

```ocaml
type lobject =
  | Fixnum of int
  | Boolean of bool
  | Symbol of string
```

And should probably handle reading it too:

```ocaml
(* This is slightly faster than Char.escaped and easier to read. *)
let stringOfChar c =
    String.make 1 c;;

let rec read_sexp stm =
  [...]
  let is_symstartchar =
      let isalpha = function | 'A'..'Z'|'a'..'z' -> true
                             | _ -> false
      in
      function | '*'|'/'|'>'|'<'|'='|'?'|'!'|'-'|'+' -> true
               | c -> isalpha c
  in
  let rec read_symbol () =
      (* Not necessary, but it otherwise messes up highlighting *)
      let literalQuote = String.get "\"" 0 in
      let is_delimiter = function | '('|')'|'{'|'}'|';' -> true
                                  | c when c=literalQuote -> true
                                  | c -> is_white c
      in
      let nc = read_char stm in
      if is_delimiter nc
      then let _ = unread_char stm nc in ""
      else stringOfChar nc ^ read_symbol ()
  in
  eat_whitespace stm;
  let c = read_char stm in
  if is_symstartchar c
  then Symbol(stringOfChar c ^ read_symbol ())
  [...]
```

It's unfortunately a little bit more complicated because we would like to read
a symbol until the next delimiter, without perhaps a specific pattern for what
is in the symbol. So we should check for a valid symbol-starting character with
`is_symstartchar`, then read a symbol with `read_symbol`.

`read_symbol` just concatenates characters until it hits a delimiter, then
backs up one. And then we construct a new `Symbol`! Not so terrible.

Of course, if you try and run it like so:

```
$ ocaml 02_symbols.ml
File "02_symbols.ml", line 87, characters 4-107:
Warning 8: this pattern-matching is not exhaustive.
Here is an example of a value that is not matched:
Symbol _
> hello
Exception: Match_failure ("02_symbols.ml", 87, 4).
$
```

OCaml will complain because it doesn't know how to `print_sexp` on a symbol. So
it will try its best but when it encounters that case, the program crashes. We
don't want to crash on symbols (they will after all be vital), so let's add a
case to `print_sexp`:

```ocaml
let rec print_sexp e =
    match e with
    | Fixnum(v) -> print_int v
    | Boolean(b) -> print_string (if b then "#t" else "#f")
    | Symbol(s) -> print_string s
```

And that should work!

```
$ ocaml 02_symbols.ml
> hello
hello
> +
+
> my-god-you've-gotten-fat
my-god-you've-gotten-fat
> ^C
$
```

Great -- It prints everything out all nicely. Now that we have symbols,
booleans, and ints, we can finally build up to collections of all of the above!

Download the code <a href="{{ page.codelink }}">here</a> if you want to mess
with it.

Next up, [lists](/blog/lisp/03_lists/).

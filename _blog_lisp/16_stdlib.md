---
title: "Writing a Lisp, Part 16: Standard Library"
author: Maxwell Bernstein
date: Mar 22, 2017
codelink: /resources/lisp/16_stdlib.ml
layout: post
---

We've gotten to the point where we can define some useful utility functions
directly by writing Lisp, which means that we can start to write a standard
library.

A standard library, if you're unfamiliar with the term, is software bundled
together in the runtime of the language. So for C it's things in `libc`
(anything you can `#include` without linking with another file), for C++ it's
the STL (`vector`, `map`, etc), for Python it's all of the non-package
imports... you get the picture.

Since we have some new I/O functions, I figure it would be useful to provide
the reader with functions like the compose function `o` (which we defined
inline in the metacircular evaluator), `println` (`print` plus newline), and
even a sorting function!

The standard library I am envisioning is a string inside the interpreter that
gets evaluated, and the resulting environment captured. Then we go on and
evaluate whatever the user wants, whether that be a REPL or a file reader.

It seems to me like this would be a good use case for OCaml's built-in `Stream`
module, and a good time to refactor some of our s-expression reading code.

The [Stream module](https://caml.inria.fr/pub/docs/manual-ocaml/libref/Stream.html)
has a pretty slick interface:

```ocaml
module type Stream = sig
  type 'a t
  exception Failure
  val of_string : string -> char t
  val of_channel : in_channel -> char t
  val next : 'a t -> 'a
  (* ... *)
end
```

Since we'll be reading from both files and strings, `of_string` and
`of_channel` seem particularly useful. We'll need to now also handle `Failure`s
when dealing with input. And we can use `next` to get the next thing in our
stream --- which for both strings and channels is a `char`. Lovely.

So there first thing we'll want to revisit is the type of `stream` (lowercase
"s") that we defined near the top of the interpreter. Since `Stream` has no
pushback buffer, we'll need to keep ours.

```ocaml
type 'a stream = {
  mutable line_num: int;
  mutable chr: char list;
  stdin: bool;
  stm: 'a Stream.t
}
```

I've added a field, `stdin`, that helps determine if the current stream is
standard input. I've also *parametrized* the type by adding a `'a` that it can
then pass to `Stream.t`. This has no bearing on the other function of the
stream. Last, I changed the field name from `chan` to `stm` so as not to be
misleading.

Updating `read_char` does not take too much effort --- only changing
`input_char` to `Stream.next`:

```ocaml
let read_char stm =
    match stm.chr with
      | [] ->
              let c = Stream.next stm.stm in
              if c = '\n' then let _ = stm.line_num <- stm.line_num + 1 in c
              else c
      | c::rest ->
              let _ = stm.chr <- rest in c
```

In order to more easily create streams, I've created some helper functions.
They neatly abstract the record away.

```ocaml
let mkstream is_stdin stm = { chr=[]; line_num=1; stdin=is_stdin; stm=stm } 
let mkstringstream s      = mkstream false     @@ Stream.of_string s
let mkfilestream f        = mkstream (f=stdin) @@ Stream.of_channel f
```

So we have `mkstream`, which is a generic stream creator. It sets `stdin` and
`stm` appropriately. Then we have `mkstringstream`, which would never be from
the standard input, so it always sets `stdin` to false and makes a string
stream. Then last we have `mkfilestream`, which sets `stdin` to `true` only
when operating on the standard input (duh).

Let's take a look now at `main`, which used to initialize a stream by hand:

```ocaml
let main =
  let ic = get_ic () in
  try  repl (mkfilestream ic) stdlib
  with Stream.Failure -> if ic <> stdin then close_in ic
```

We still get an input channel, but then we make a file stream from it. No more
records in `main`! Hurrah!

And of course you're wondering now, "what is this `stdlib` variable"? As well
you should, because it's kind of clunky but also kind of great.

```ocaml
let stdlib =
  let ev env e =
    match e with
    | Defexp d -> evaldef d env
    | _ -> raise (TypeError "Can only have definitions in stdlib")
  in
  let rec slurp stm env =
    try  stm |> read_sexp |> build_ast |> ev env |> snd |> slurp stm
    with Stream.Failure -> env
  in
  let stm = mkstringstream "(define o (f g) (lambda (x) (f (g x))))"
  in slurp stm basis
```

Instead of making a file stream for the standard library, we want to make a
string stream and read from some embedded string. This allows us to ship a
standard library easily with the interpreter without dealing with a filesystem.

Once we have a string stream, we have to read an s-expression, build an AST
from that s-expression, evaluate the definition, grab the mutated environment
from that definition, and then continue reading until we're done
(`Stream.Failure`). That problem lends itself super nicely to the `|>`
operator, which acts much like the Unix pipe (`|`).

`slurp` does exactly what I described in English above (apply a series of
transforms one after the other until end of input) with only minor gross-ness
in `ev`.

With `ev` I did two minorly gross things:

1. Switch the order of the arguments so that `ev env` evaluates to a curried
   function that expects just one argument: an expression
2. Allow only definitions in the standard library

The second thing has two reasons:

1. We don't want arbitrary, potentially side-effecting code in the standard
   library. Nobody wants to see a print statement executed every time!
2. Non-`Def`s don't mutate the environment *at all*, so there would be no
   changes that we care about anyway. `evaldef` is kind enough to return both
   the expression *and* the new environment.

Since we don't want to only define the composition function `o`, but also I did
not want to inline a massive string in the small code snippet above, I have
reprocuced the full standard library below:

```common-lisp
(define o (f g) (lambda (x) (f (g x))))
(val caar (o car car))
(val cadr (o car cdr))
(val caddr (o cadr cdr))
(val cadar (o car (o cdr car)))
(val caddar (o car (o cdr (o cdr car))))

(val cons pair)

(val newline (itoc 10))
(val space (itoc 32))

; This is pretty awkward looking because we have no other way to sequence
; operations. We have no begin, nothing.
(define println (s)
  (let ((ok (print s)))
    (print newline)))

; This is less awkward because we actually use ic and c.
(define getline ()
  (let* ((ic (getchar))
         (c (itoc ic)))
    (if (or (eq c newline) (eq ic ~1))
      empty-symbol
      (cat c (getline)))))

(define null? (xs)
  (eq xs '()))

(define length (ls)
  (if (null? ls)
    0
    (+ 1 (length (cdr ls)))))

(define take (n ls)
  (if (or (< n 1) (null? ls))
    '()
    (cons (car ls) (take (- n 1) (cdr ls)))))

(define drop (n ls)
  (if (or (< n 1) (null? ls))
    ls
    (drop (- n 1) (cdr ls))))

(define merge (xs ys)
  (if (null? xs)
    ys
    (if (null? ys)
      xs
      (if (< (car xs) (car ys))
        (cons (car xs) (merge (cdr xs) ys))
        (cons (car ys) (merge xs (cdr ys)))))))

(define mergesort (ls)
  (if (null? ls)
    ls
    (if (null? (cdr ls))
      ls
      (let* ((size (length ls))
             (half (/ size 2))
             (first (take half ls))
             (second (drop half ls)))
        (merge (mergesort first) (mergesort second))))))
```

Fun fact: in my writing of `mergesort` I discovered a rather nasty bug in
`let*`, which would have been evident had I written out the desired behavior
beforehand. Whoops. The bug was thus:

\<problem\>

So say the programmer wrote something like this:

```scheme
(val x 1)
(let* ((a x)) a)
```

The interpreter would see that and raise `NotFound "x"`, which is obviously
wrong because *of course* `x`, defined right above, should be in scope for the
let-expression. This happened because at evaluation-time for `let*`, it bound a
lot of variables by using `List.fold_left` onto an empty list, then extended
the current environment with that new one.

The `fold_left` on the empty list is the problem here. How can `evbinding`
*possibly* make a binding of `a->x` if the empty environment doesn't contain
`x`?

A better solution, which I have included in the interpreter attached to this
post, is as follows:

```ocaml
    [...]
    | Let (LETSTAR, bs, body) ->
        let evbinding acc (n, e) = bind (n, evalexp e acc, acc) in
        evalexp body (List.fold_left evbinding env bs)
    [...]
```

This instead *starts* with the existing environment and evaluates each binding
in turn from there.

These kinds of problems make clear the need for rigorous interpreter testing,
something I hope to eventually cover.

\</problem\>

Last, on a note that is completely unrelated to the standard library, sometime
over the last few posts I got frustrated with the level of error reporting in
the interpreter and thought it would be nice to have an actual backtrace of
what the heck was going on when debugging one of these irritating problems.

I thought a good solution would be to repeatedly catch and raise errors in
`evalexp` all the way up until `repl`. That is, if in some complicated
expression a small node tens of layers deep in the AST has an issue, the error
propagates up through each level of the AST, printing the expression as it
goes. Here's what I came up with:

```ocaml
let rec evalexp exp env =
  [...]
  let rec ev = function
    [...]
  in
  try
    ev exp
  with e ->
    (
      let err = Printexc.to_string e in
      print_endline @@ "Error: '" ^ err ^ "' in expression " ^ string_exp exp;
      raise e
    )
```

I'd be interested to hear people's thoughts on it. It works fine for me, but
it's kind of noisy and also carries no information about the raw source of the
program --- just the stringified representation of the AST.

Anyway, that's all I've got for standard libraries. I'll probably add things to
this one in the future.

Download the code [here]({{ page.codelink }}) if you want to mess with it.

Next up, the post I have been waiting quite some time for:
[modules](/blog/lisp/17_modules/).

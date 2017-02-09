---
title: "Writing a Lisp, Part 11: Primitives III"
codelink: /resources/lisp/11_prim3.ml
date: Feb 8, 2017
---

In order to have a complete Lisp, to be able to write a metacircular evaluator,
we need to add a couple more primitives, and perhaps also refine the way we add
primitives to our basis.

According to [this StackOverflow answer](http://stackoverflow.com/a/3484206),
the required functions for building a Lisp are:

1. `atom?`
2. `car`
3. `cdr`
4. `cons`/`pair`
5. `quote`
6. `cond`/`if`
7. `lambda`
8. `label`/`define`

Although with some [fun mathy things](http://mvanier.livejournal.com/2897.html)
you can do away with `define`.

This proposed Lisp does not have numbers, though they could reasonably easily
be implemented using [Church numerals](https://en.wikipedia.org/wiki/Church_encoding#Church_numerals). We've
opted to make our lives a wee bit easier and our Lisp quite a bit faster by
instead using OCaml's built-in integer type.

The proposed Lisp *also* does not have the boolean type, and that is because
the symbol/atom `t` is considered truthy, and everything else considered
falsey.

In any case, let's add `atom?`, `car`, and `cdr`:

```ocaml
let basis =
  [...]
  let prim_car = function
    | [Pair (car, _)] -> car
    | _ -> raise (TypeError "(car non-nil-pair)")
  in
  let prim_cdr = function
    | [Pair (_, cdr)] -> cdr
    | _ -> raise (TypeError "(cdr non-nil-pair)")
  in
  let prim_atomp = function
    | [Pair (_, _)] -> Boolean false
    | [_] -> Boolean true
    | _ -> raise (TypeError "(atom? something)")
  in
  [...]
  List.fold_left newprim [] [
    [...]
    ("car", prim_car);
    ("cdr", prim_cdr);
    ("atom?", prim_atomp)
  ]
```

This should be fairly self-explanatory. Note that we're defining atoms now as
any non-`Pair` value. So `nil` is an atom, `a` is an atom, `21` is an atom,
etc.

Assuming we're going to keep the booleans and the numbers (which we are), we'll
also probably want some more primitives, like:

1. `+`, `-`, `*`, `/`
2. `<`, `=`, `>`

Note that these are two separate classes of function: one class takes in two
numbers and returns a number, and the other class takes in two numbers and
returns a boolean. This generalization about the two classes helps us write
a terse and minimally repetitive basis.

```ocaml
let basis =
  let numprim name op =
    (name, (function [Fixnum a; Fixnum b] -> Fixnum (op a b)
            | _ -> raise (TypeError ("(" ^ name ^ " int int)"))))
  in
  let cmpprim name op =
    (name, (function [Fixnum a; Fixnum b] -> Boolean (op a b)
            | _ -> raise (TypeError ("(" ^ name ^ " int int)"))))
  in
  [...]
  List.fold_left newprim [] [
    numprim "+" (+);
    numprim "-" (-);
    numprim "*" ( * );
    numprim "/" (/);
    cmpprim "<" (<);
    cmpprim ">" (>);
    cmpprim "=" (=);
    [...]
  ]
```

This works nicely in OCaml because OCaml operators like `+` and `>` are just
functions with signatures like `int -> int -> int` and `int -> int -> bool`, so
can be passed around like we do above.

Note that I've added spaces around the `*` in `( * )` because otherwise OCaml
would think that it was the start or end of a comment.

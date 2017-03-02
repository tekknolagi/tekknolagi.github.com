---
title: "Writing a Lisp, Part 11: Primitives III"
codelink: /resources/lisp/11_prim3.ml
date: Feb 9, 2017
---

In order to have a complete Lisp, to be able to write a metacircular evaluator,
we need to add a couple more primitives, and perhaps also refine the way we add
primitives to our basis.

According to [this StackOverflow answer](http://stackoverflow.com/a/3484206),
the required functions for building a Lisp are:

1. `atom?`
2. `car`
3. `cdr`
4. `cons`/`pair` -- done
5. `quote` -- done
6. `cond`/`if` -- done
7. `lambda` -- done
8. `label`/`define` -- done

Although with some [fun mathy things](http://mvanier.livejournal.com/2897.html)
you can do away with `define`.

This proposed Lisp does not have numbers, though they could reasonably easily
be implemented using [Church numerals](https://en.wikipedia.org/wiki/Church_encoding#Church_numerals).
We've opted to make our lives a wee bit easier and our Lisp quite a bit faster
by instead using OCaml's built-in integer type.

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

### Syntax transforms with `cond`

I'd like to also add something that makes our lives a little bit easier ---
`cond`.  Technically a `cond` is equivalent to a long chain of `if`s, but in
every single implementation of Lisp in Lisp that I've seen (something that I've
been hinting is coming for some time), the author uses `cond` because it's much
cleaner. Instead of adding a whole new AST type for it, though, we're going to
just make a syntax transformation to chained `if`s.

```ocaml
let rec build_ast sexp =
  let rec cond_to_if = function
    | [] -> Literal (Symbol "error")
    | (Pair(cond, Pair(res, Nil)))::condpairs ->
        If (build_ast cond, build_ast res, cond_to_if condpairs)
    | _ -> raise (TypeError "(cond conditions)")
  in
  match sexp with
  [...]
  | Pair _ when is_list sexp ->
      (match pair_to_list sexp with
      [...]
      | (Symbol "cond")::conditions -> cond_to_if conditions
  [...]
```

The way I've set up the code, the following are equivalent:

```scheme
(cond ((< x 4) 'lower)
      ((= x 4) 'equal)
      ((> x 4) 'higher))

(if (< x 4)
  'lower
  (if (= x 4)
    'equal
    (if (> x 4)
      'higher
      'error)))
```

This is because we have no error handling at the moment, in two senses:

1. The language we interpret doesn't have exceptions or a way to indicate
   errors, except for continuation-passing, which is unoptimized.
2. The moment our interpreter encounters an error (say, a `TypeError`), it
   halts.

Both of these are suboptimal and should be touched on in future posts.

Download the code [here]({{ page.codelink }}) if you want to mess with it.

That just about wraps up our Lisp implementation -- it can now be considered
reasonably fully complete (minus I/O and all that). For some proof of that...
next up, the [metacircular evaluator](../12_metacircular/).

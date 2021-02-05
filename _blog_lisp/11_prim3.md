---
title: "Writing a Lisp, Part 11: Primitives 3"
author: Maxwell Bernstein
date: Feb 9, 2017
codelink: /resources/lisp/11_prim3.ml
layout: post
---

In order to have a complete Lisp, to be able to write a metacircular evaluator,
we need to add a couple more primitives, and perhaps also refine the way we add
primitives to our basis.

According to [this StackOverflow answer](http://stackoverflow.com/a/3484206),
the required functions for building a Lisp are:

1. `atom?`
2. `eq`
3. `car`
4. `cdr`
5. `cons`/`pair` -- done
6. `quote` -- done
7. `cond`/`if` -- done
8. `lambda` -- done
9. `label`/`define` -- done

Although with some [fun mathy things](http://mvanier.livejournal.com/2897.html)
you can do away with `define`.

This proposed Lisp does not have numbers, though they could reasonably easily
be implemented using [Church numerals](https://en.wikipedia.org/wiki/Church_encoding#Church_numerals).
We've opted to make our lives a wee bit easier and our Lisp quite a bit faster
by instead using OCaml's built-in integer type.

The proposed Lisp *also* does not have the boolean type, and that is because
the symbol/atom `t` is considered truthy, and everything else considered
falsey.

In any case, let's add `atom?`, `car`, `cdr`, and `eq`:

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
  let prim_eq = function
    | [a; b] -> Boolean (a=b)
    | _ -> raise (TypeError "(eq a b)")
  in
  [...]
  List.fold_left newprim [] [
    [...]
    ("car", prim_car);
    ("cdr", prim_cdr);
    ("eq", prim_eq);
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

### Mutually recursive functions, sort of

I've also done a sneaky thing in supporting mutually recursive functions. I
added an OCaml function `extend` that takes two environments and stacks them,
so that the contents of both are queryable.

We then use this `extend` function when evaluating a closure --- we evaluate
the closure in the combined closure environment and current evaluation
environment:

```ocaml
let extend newenv oldenv =
  List.fold_right (fun (b, v) acc -> bindloc (b, v, acc)) newenv oldenv

let rec evalexp exp env =
  let evalapply f vs =
    match f with
    | Primitive (_, f) -> f vs
    | Closure (ns, e, clenv) ->
        evalexp e (extend (bindlist ns vs clenv) env)
    | _ -> raise (TypeError "(apply prim '(args)) or (prim args)")
  in
  [...]
```

Why is this important? Well, consider the following (admittedly contrived)
case:

```scheme
(define f (x)
  (if (< x 2)
      1
      (g (- x 1))))

(define g (x)
  (if (< x 2)
      3
      (f (- x 2))))
```

It won't work unless `f` and `g` know about one another at evaluation time.
We'll see a more useful, real-world example of mutually recursive functions in
the next section.

Download the code [here]({{ page.codelink }}) if you want to mess with it.

That just about wraps up our Lisp implementation -- it can now be considered
reasonably fully complete (minus I/O and all that). For some proof of that...
next up, the [metacircular evaluator](/blog/lisp/12_metacircular/).

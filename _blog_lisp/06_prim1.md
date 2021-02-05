---
title: "Writing a Lisp, Part 6: Primitives 1"
author: Maxwell Bernstein
date: Dec 27, 2016
codelink: /resources/lisp/06_prim1.ml
layout: post
---

Last time we added environments to our Lisp, but they are not much use in their
current state - there's no way to interact with them from inside the REPL. So
this time we're going to add a primitive, `val`, to define variables. We'll use
`val` like so:

```scheme
(val x 5) ;; Returns: 5
x         ;; Returns: 5
(val x 7) ;; Returns: 7
x         ;; Returns: 7
```

Here, `val` changes the apparent value of `x`, but not by modifying the
contents; instead it will make a new binding in the current scope. Essentially
just a wrapper for `bind`. So the environment starts off as `()` or `nil`, then
looks like `((x . 5))`, then `((x . 7) (x . 5))`.

We'll implement `val` much in the same way we implemented `if` --- a case in
`match`:

```ocaml
let rec eval_sexp sexp env =
    match sexp with
    [...]
    | Pair(Symbol "if", Pair(cond, Pair(iftrue, Pair(iffalse, Nil)))) ->
            eval_sexp (eval_if cond iftrue iffalse) env
    | Pair(Symbol "val", Pair(Symbol name, Pair(exp, Nil))) ->
            let (expval, _) = eval_sexp exp env in
            let env' = bind (name, expval, env) in
            (expval, env')
    [...]
```

It evaluates the expression, binds the name to that value, and then returns the
value and modified environment.

Binding a name to the value is all well and good, but not much use if we can't
access the value. So let's return to our evaluation of `Symbol`s --- there's
some work to be done. Currently it looks like this:

```ocaml
let rec eval_sexp sexp env =
    match sexp with
    [...]
    | Symbol(v) -> (Symbol(v), env)
    [...]
```

We probably want to, instead of just returning the `Symbol` unmodified, look up
the corresponding value to that name. So let's use that function that we
defined:

```ocaml
let rec eval_sexp sexp env =
    match sexp with
    [...]
    | Symbol(name) -> (lookup (name, env), env)
    [...]
```

And there we have it! Let's give it a go:

```
$ ocaml 06_prim1.ml
> (val x 5)
5
> x
5
> (val x 7)
7
> x
7
> Exception: End_of_file.
$
```

Neat. But the code is still pretty clunky and could definitely be improved.
Since we know that all primitive calls (for now just `if` and `val`) will be
lists (`Pair`s ending with `Nil`), why not just convert and save the pattern
matching headache?

If it's not a list, it's some pair (we still don't have a way of generating
those), and should just be returned as-is. If it's not a function call we
recognize, just return it as-is for now.

```ocaml
let rec eval_sexp sexp env =
    [...]
    match sexp with
    [...]
    | Pair(_, _) when is_list sexp ->
            (match pair_to_list sexp with
                 | [Symbol "if"; cond; iftrue; iffalse] ->
                         eval_sexp (eval_if cond iftrue iffalse) env
                 | [Symbol "env"] -> (env, env)
                 | [Symbol "val"; Symbol name; exp] ->
                         let (expval, _) = eval_sexp exp env in
                         let env' = bind (name, expval, env) in
                         (expval, env')
                 | _ -> (sexp, env)
            )
    | _ -> (sexp, env)
```

Note that this requires pulling `is_list` out of `print_sexp` for use in
`eval_sexp`.

Also note that I've added in a neat little primitive, `env`, that shows the
current environment. This will be helpful with debugging, and is also useful in
demonstrating that our functions work as we expect. For example:

```
$ ocaml 06_prim1.ml
> (env)
nil
> (val x 5)
5
> (env)
((x . 5))
> (val x 7)
7
> (env)
((x . 7) (x . 5))
> Exception: End_of_file.
$
```

How about we finally get around to making some pairs? I think that could be
fun --- and another one-liner!

```ocaml
let rec eval_sexp sexp env =
    [...]
    | Pair(_, _) when is_list sexp ->
            (match pair_to_list sexp with
                 | [Symbol "if"; cond; iftrue; iffalse] ->
                         eval_sexp (eval_if cond iftrue iffalse) env
                 | [Symbol "env"] -> (env, env)
                 | [Symbol "pair"; car; cdr] ->  (* new! *)
                         (Pair(car, cdr), env)   (* new! *)
                 | [Symbol "val"; Symbol name; exp] ->
                         let (expval, _) = eval_sexp exp env in
                         let env' = bind (name, expval, env) in
                         (expval, env')
                 | _ -> (sexp, env)
            )
    | _ -> (sexp, env)
```

Well, two lines, if (like me) you are loath to break the column boundary. And
let's see it in action:

```
$ ocaml 06_prim1.ml
> (pair 3 4)
(3 . 4)
> (val x (pair 3 4))
(3 . 4)
> (env)
((x . (3 . 4)))
> Exception: End_of_file.
$
```

Download the code [here]({{ page.codelink }}) if you want to mess with it.

Next up, [primitives II](/blog/lisp/07_prim2/).

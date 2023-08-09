---
title: "Writing a Lisp, Part 7: Primitives 2"
author: Maxwell Bernstein
date: Jan 18, 2017
codelink: /resources/lisp/07_prim2.ml
layout: post
---

Right now the way we handle evaluation of primitive functions is less than
ideal -- each requires a separate clause in `eval`:

```ocaml
let rec eval_sexp sexp env =
    [...]
    | Pair(_, _) when is_list sexp ->
            (match pair_to_list sexp with
                 | [Symbol "if"; cond; iftrue; iffalse] ->
                         eval_sexp (eval_if cond iftrue iffalse) env
                 | [Symbol "env"] -> (env, env)
                 | [Symbol "pair"; car; cdr] ->
                         (Pair(car, cdr), env)
                 | [Symbol "val"; Symbol name; exp] ->
                         let (expval, _) = eval_sexp exp env in
                         let env' = bind (name, expval, env) in
                         (expval, env')
                 | _ -> (sexp, env)
            )
    | _ -> (sexp, env)
```

This, as you can imagine, makes life repetitive and bug-prone. In fact,
there's even a bug in the code above. Can you spot it?

**I've forgotten to evaluate the `car` and `cdr` in the `pair` primitive.**
`pair` should evaluate its arguments and make the values into the pair --- not
the original expressions. [^opsem]

In this post we will explore a means for having the same plumbing for all
primitive functions. For example... what if we could store them in an
environment? That would be pretty neat. Then we could look them up, store them
in data structures, etc. And `eval` would look something like:

```ocaml
let rec eval_sexp sexp env =
    [...]
    match sexp with
    [...]
    | Primitive(n,f) -> (Primitive(n,f), env)
    | Pair(_, _) when is_list sexp ->
            (match pair_to_list sexp with
                 | [Symbol "if"; cond; iftrue; iffalse] ->
                         eval_sexp (eval_if cond iftrue iffalse) env
                 [...]
                 | (Symbol fn)::args ->
                         (match eval_sexp (Symbol fn) env with
                              | (Primitive(n, f), _) -> (f args, env)
                              | _ -> raise (TypeError "(apply func args)"))
                 | _ -> (sexp, env)
            )
    | _ -> (sexp, env)
```

with the new type for `lobject` looking like:

```ocaml
type lobject =
  | Fixnum of int
  | Boolean of bool
  | Symbol of string
  | Nil
  | Pair of lobject * lobject
  | Primitive of string * (lobject list -> lobject)     (* NEW *)
```

I store the name as a string in the `Primitive` constructor because it makes
for cleaner printing. If I can say `#<primitive:list>` instead of
`#<primitive>` with not much additional effort, why not go ahead and do that?

The difference in this implementation is that we would just have to handle
*special forms* separately. A special form is a type of expression that does
not follow the normal rules of evaluation (evaluate the arguments, then apply
the function). Great examples of special forms so far are `if` and `val`:

* An `if`-expression is not "normal" in that its component expressions are only
  conditionally evaluated. In most other function-call-looking-things, all of
  the arguments are evaluated first.
* A `val`-expression is not normal in that the new environment with the binding
  is not discarded, but kept.

Speaking of `if`, the mistake I made earlier is to keep the resulting
environment. Let's rectify that now:

```ocaml
let rec eval_sexp sexp env =
    [...]
    match sexp with
    [...]
    | Primitive(n,f) -> (Primitive(n,f), env)
    | Pair(_, _) when is_list sexp ->
            (match pair_to_list sexp with
                 | [Symbol "if"; cond; iftrue; iffalse] ->
                         let (ifval, _) =
                               eval_sexp (eval_if cond iftrue iffalse) env
                         in
                         (ifval, env)
    [...]
    | _ -> (sexp, env)
```

That's really the only change we need --- ignore the resulting environment and
we're good to go. Sneak peek, by the way, of the new semantics of `if`
(expressed in Big-Step Operational Semantics):

```
(cond, rho) -> (true, rho')   (e1, rho) -> (v1, rho'') 
------------------------------------------------------ IFTRUE
(IF(cond, e1, e2), rho) -> (v1, rho)

(cond, rho) -> (false, rho')   (e2, rho) -> (v2, rho'') 
------------------------------------------------------- IFFALSE
(IF(cond, e1, e2), rho) -> (v2, rho)
```

These are two separate *judgement forms* for two separate cases of an
`if`-expression: the condition evaluates to `true` or the condition evaluates
to `false`.

In brief, `->` means "evaluates to", and here we are evaluating pairs of `exp *
env`. These pairs evaluate to pairs of `val * env`, where the environment may
have changed. It is common to use `rho` to denote your environment. It is also
common to use `sigma`. They are slightly different. Above the line are
preconditions. If they are satisfied, the part below the line can happen.
*Does* happen.

I'll talk more about operational semantics ("opsem", colloquially... at least
at Tufts) later.

**Anyway.** Back to adding primitives to the environment. Let's add two
primitives: `pair`, which we had above, and `+`, so we can add two numbers
(it's about time):

```ocaml
let basis =
    let prim_plus = function
        | [Fixnum(a); Fixnum(b)] -> Fixnum(a+b)
        | _ -> raise (TypeError "(+ int int)")
    in
    let prim_pair = function
        | [a; b] -> Pair(a, b)
        | _ -> raise (TypeError "(pair a b)")
    in
    let newprim acc (name, func) =
        bind (name, Primitive(name, func), acc)
    in
    List.fold_left newprim Nil [
        ("+", prim_plus);
        ("pair", prim_pair)
       ]
```

This is slightly tricky but all it does is allow us to bind a list of
primitives to their names instead of manually writing out `Pair(Symbol "+",
Primitive(Symbol "+", ...`.

Also note that we've only defined `+` for `Fixnum`s, but we could also easily
define it for `Symbol`s by having it concatenate their string values. Food for
thought.

Download the code [here]({{ page.codelink }}) if you want to mess with it.

We can see that our code is getting kind of clunky to work with. Who the heck
wants to manipulate what is just *screaming* at us to be a tree as a list?
Certainly not me. It's messy and occasionally difficult to get right. So:

Next up, [ASTs](/blog/lisp/08_asts/).

[^opsem]:
    There is a formal and programming-language independent way to specify the
    expected behavior of a programming language called [Big-Step Operational
    Semantics](http://web.archive.org/web/20150323191125/https://www.cs.tufts.edu/comp/105/lectures/opsem.pdf).
    With this, it would have been clear that `pair` should definitely evaluate
    each of its arguments, then form a `Pair` of the values. We could
    completely write out the expected behavior of this Lisp before ever sitting
    down to write the interpreter, and that would be great in theory, but it
    would mean a couple of bad things for us in practice:

    * People reading this series would not be able to jump right in and learn about
    building a simple interpreter
    * It would be a huge hurdle for people who have not studied any math or formal
    logic

    I plan on introducing operational semantics at some point in this series, time
    permitting --- but there are so many other exciting topics! [^roadmap]

[^roadmap]:
    Can footnotes have footnotes? I want to cover so many topics in this series,
    such as (in no particular order):

    * A modular interpreter, with different approaches to a Reader, Printer,
      Evaluator, ErrorHandler, etc
    * Additional syntax or syntactic sugar
    * Testing
    * Type checking
    * Type inference
    * Compilation to a reasonable architecture (as in, not x86)
    * Operational semantics
    * A metacircular evaluator (i.e. this Lisp written in itself!)
    * ...and more!

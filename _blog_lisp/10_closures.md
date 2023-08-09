---
title: "Writing a Lisp, Part 10: Closures"
author: Maxwell Bernstein
date: Feb 7, 2017
codelink: /resources/lisp/10_closures.ml
layout: post
---

Isn't it about time we were able to define our own functions? I think so.
There's just one problem: some of the plumbing we've built so far doesn't suit
what we're making in this post. Our environments, for example, won't support
recursive functions --- so we'll need to fix the implementation. Also, fair
warning... *this is the longest post yet!*

### The problem with our environments

Say we're defining a factorial function. It might look like this:

```scheme
(define fact (x) (if (< x 2) 1 (* x (fact (- x 1)))))
```

So in order to evaluate the definition and bind `fact`, we'd have to do the
following:

1. Capture the body of the `define` in a *closure* (don't worry about the exact
   mechanics just yet)
2. Bind the name `fact` in an environment, just like we do when evaluating
   `val`
3. Return the new expression and environment

That's all fine and dandy until we try and execute `fact`, at which point the
interpreter will halt with a `NotFound` exception. After all, how can `fact`
possibly know what it itself is? It can't, because our environments store
environments as `Pair`s of `Symbol` *x* `value`, so we'd have to infinitely
recursively store the contents of the function in nested environments all the
way down. And that takes up too much space.

Or would we have to? We could try fetching the formal parameters from the
closure environment and the recursive function calls from the call-time
environment...  but that won't work, because that function might have been
redefined in the meantime, like so:

```scheme
; Assume for a moment we've defined <, *, - primitives
(define fact (x) (if (< x 2) 1 (* x (fact (- x 1)))))
(val x fact)
(define fact (x) 5)
(x 3) ; == (* 3 (fact (- 3 1)))
      ; == (* 3 5)
      ; == 15
```

So we've got to fix them.

### New ideas for environments

In C, we solve problems of recursive data storage with pointers. If, for
example, you need to represent a binary search tree, you might do so like this:

```c
struct TreeNode {
    void *data;
    struct TreeNode *left;
    struct TreeNode *right;
};
```

Even if we wanted to, we couldn't store *actual* `TreeNode`s for the left and
right subtrees of any given node, because (as above) this would take infinite
space.

In OCaml, we'll solve this problem with `ref`s, which are sort of like OCaml's
well-typechecked version of pointers. [^refs]

I propose that we change our environment to have the following type:

```ocaml
type 'a env = (string * 'a option ref) list
```

The change to the built-in `list` type from our use of `Pair`s is largely due
to `lobject`'s inability to hold `option` or `ref`. It comes with benefits,
most notably the decrease in code complexity throughout the interpreter.

That's a lot of gymnastics for a lousy environment, but here's the rationale,
in brief: we need to fix the steps from above (reproduced here with
modifications in **bold**):

1. Capture the body of the `define` in a *closure* (don't worry about the exact
   mechanics just yet), **binding `fact` to a `ref` of unspecified value**
2. Bind the name `fact` **to the closure from above**, just like we do when
   evaluating `val`
3. **Update the `ref` from step 1 to point to the resulting closure**
4. Return the new expression and environment

These updates allow us to "close the loop" and make `fact` self-aware. In our
case, "unspecified value" means using the `option` type
[^option-type], and we'll have a `ref` pointing to that.

Look for these modifications and additions to the environment functions below.

### What are closures anyway?

Earlier I said not to worry about the exact mechanics of closures just yet.
Well, time to worry about the exact mechanics. First, the *how*.

A `lambda` is an anonymous function, defined like so:

```scheme
(lambda (x) (+ x 1))
;           ^ the expression body
;       ^ the formal parameters
; ^ the word lambda
```

This anonymous function takes in a value (called `x` internally) and adds one
(1) to it. Evaluating a lambda-expression results in a *closure*. A closure is
composed of three things:

* Formal parameters (in this case, just `x`)
* Body expression (in this case, `(+ x 1)`)
* Captured environment (in this case... the basis? Maybe? We don't really
  know.)

The captured environment is key, and is what allows us to do things like
recursive functions, curried functions, and a bunch of other fancy stuff. In
order to make a closure, we can simply capture the environment at the point
where the `lambda` is being evaluated.

A closure is a value, and therefore self-evaluating. They are only called into
action when applied to values, at which point:

1. The values they are applied to are matched with the formal parameters
2. That new environment is combined with the captured environment
3. The body is evaluated in that new combined environment, and
4. A value is returned

And that's closures in a nutshell. So let's hop to it.

### Making closures work

We'll need to have lambda expressions and closure `value`s, I suppose, so let's
add those:

```ocaml
type lobject =
  | Fixnum of int
  | Boolean of bool
  | Symbol of string
  | Nil
  | Pair of lobject * lobject
  | Primitive of string * (lobject list -> lobject)
  | Quote of value
  | Closure of name list * exp * value env              (* NEW! *)

and value = lobject
and name = string
and exp =
  | Literal of value
  | Var of name
  | If of exp * exp * exp
  | And of exp * exp
  | Or of exp * exp
  | Apply of exp * exp
  | Call of exp * exp list
  | Lambda of name list * exp                           (* NEW! *)
  | Defexp of def
```

Note how the only difference between `Lambda` and `Closure` is the attached
`value env`.

But so far we don't actually have a means to construct `Lambda` expressions, so
let's go ahead and add that into `build_ast`:

```ocaml
let rec build_ast sexp =
  match sexp with
  | Primitive _ | Closure _ -> raise ThisCan'tHappenError
  | Fixnum _ | Boolean _ | Nil | Quote _ -> Literal sexp
  | Symbol s -> Var s
  | Pair _ when is_list sexp ->
      (match pair_to_list sexp with
      [...]
      | [Symbol "val"; Symbol n; e] -> Defexp (Val (n, build_ast e))
      | [Symbol "lambda"; ns; e] when is_list ns ->
          let err () = raise (TypeError "(lambda (formals) body)") in
          let names = List.map (function Symbol s -> s | _ -> err ())
                               (pair_to_list ns)
          in Lambda (names, build_ast e)
  [...]
```

We've got to make sure of a couple things:

1. The list starts off with "lambda"
2. We have a list of `Symbol` names for formal parameters
3. We can build an AST from the given body expression

I've included a handy-dandy error function, tersely dubbed `err`, that will
raise an exception if any of the supposed "formals" are not, in fact, formals.

And that's all well and good if we can build it, but right now `evalexp`
doesn't know how to handle it. So let's make some `Closure`s!

```ocaml
let rec evalexp exp env =
  [...]
  let rec ev = function
    [...]
    | Call (Var "env", []) -> env_to_val env
    | Call (e, es) -> evalapply (ev e) (List.map ev es)
    | Lambda (ns, e) -> Closure (ns, e, env)
    | Defexp d -> raise ThisCan'tHappenError
  in ev exp
```

Creating closures is surprisingly easy to do! Now before we try and evaluate
them, let's get cracking with some environment code.

`bind` remains largely the same:

```ocaml
let bind (n, v, e) = (n, ref (Some v))::e
```

Only now we're sprinkling in `ref`s and `option`s. We'll also introduce two
other functions, `mkloc` and `bindloc`, which we'll use later on:

```ocaml
let mkloc () = ref None
let bindloc (n, vor, e) = (n, vor)::e

(* Signature of bindloc: *)
val bindloc : name * 'a option ref * a env -> 'a env
```

Next comes `lookup`, which is also modified to deal with `ref`s and `option`s:

```ocaml
exception UnspecifiedValue of string

let rec lookup = function
  | (n, []) -> raise (NotFound n)
  | (n, (n', v)::_) when n=n' ->
     begin
       match !v with
       | Some v' -> v'
       | None -> raise (UnspecifiedValue n)
     end
  | (n, (n', _)::bs) -> lookup (n, bs)
```

We've got one more case to handle now --- when the value at a location is
`None`. In that case, we should raise an `UnspecifiedValue` exception.
Otherwise, we'll just do as we did before, albeit now with some more
complexity.

Last, we've got a `bindlist` function whose sole purpose is to bind a bunch of
`name`s to a bunch of `value`s (putting them in `option ref`s along the way).

```ocaml
let bindlist ns vs env =
  List.fold_left2 (fun acc n v -> bind (n, v, acc)) env ns vs
```

That's all for environments... for now.

Okay but we still don't know how to evaluate `Closure`s... currently `Call` and
`Apply` both use `evalapply`, but that only has a case for `Primitive`s. It'll
need to be able to handle `Closure`s as well:

```ocaml
let rec evalexp exp env =
  let evalapply f vs =
    match f with
    | Primitive (_, f) -> f vs
    | Closure (ns, e, clenv) -> evalexp e (bindlist ns vs clenv)
    | _ -> raise (TypeError "(apply prim '(args)) or (prim args)")
  in
  [...]
```

Ah, yes. This is where `bindlist` comes in handy. Bind the names of the formals
to the values of the actuals, add them to the captured `Closure` environment...
and then just call `evalexp`. That's it! How crazy is that?? Now we can build
functions quite easily:

```
$ ocaml 10_closures.ml
> (val addone (lambda (x) (+ x 1)))
#<closure>
> (addone 4)
5
> Exception: End_of_file.
$
```

I've glossed over the printing of closures, since (as you can see) all it
requires is printing `#<closure>`. You could make it fancier and actually print
out the lambda and captured environment, but I think you'll find most of the
time that's just too much output.

Astute readers will at this point notice that we haven't yet supported
recursion, or really even done anything notably different with the new
implementation of environments. Yes, well. That's in the next section.

### Defining `define`

We have no way to make the values bound with `val` self-aware, so we're going
to do that now with `define`. Let's add a new type of `def`:

```ocaml
[...]
and def =
  | Val of name * exp
  | Def of name * name list * exp                       (* NEW! *)
  | Exp of exp
```


Note that `Def` that very much mirrors the syntax we used before:

```scheme
(define f (x) (+ x 1))
; Def ("f", ["x"], Call(Literal (Primitive "+"), [Var "x"; Literal (Fixnum 1)))
```

But we don't yet have a way to *build* `Def`s, so let's go ahead and do that.
We're going to need to add yet another case in `build_ast` that looks very
similar for the case that builds `Lambda`s:

```ocaml
let rec build_ast sexp =
  [...]
      (match pair_to_list sexp with
      [...]
      | [Symbol "lambda"; ns; e] when is_list ns ->
          let err () = raise (TypeError "(lambda (formals) body)") in
          let names = List.map (function Symbol s -> s | _ -> err ())
                               (pair_to_list ns)
          in Lambda (names, build_ast e)
      | [Symbol "define"; Symbol n; ns; e] ->
          let err () = raise (TypeError "(define name (formals) body)") in
          let names = List.map (function Symbol s -> s | _ -> err ())
                               (pair_to_list ns)
          in Defexp (Def (n, names, build_ast e))
      [...]
```

We've got nearly the same structure --- the only differences now are that we
have to capture the name of the function we're defining inside the `Def` and
then wrap the `Def` in a `Defexp`.

Now that we can build `Def`s we should also be able to evaluate them. So let's
head on over to `evaldef`, where we're add a new case. I'm going to reproduce
the English steps I outlined above and then give the code so you can see how
well they translate over.

#### English

1. Capture the body of the `define` in a *closure* binding the function name to
   a `ref` of unspecified value
2. Bind the function name to the closure from above, just like we do when
   evaluating `val`
3. Update the `ref` from step 1 to point to the resulting closure
4. Return the new expression and environment

#### OCaml

```ocaml
let evaldef def env =
  match def with
  | Val (n, e) -> let v = evalexp e env in (v, bind (n, v, env))
  | Def (n, ns, e) ->
      let (formals, body, cl_env) =
          (match evalexp (Lambda (ns, e)) env with
           | Closure (fs, bod, env) -> (fs, bod, env)
           | _ -> raise (TypeError "Expecting closure."))
      in
      let loc = mkloc () in
      let clo = Closure (formals, body, bindloc (n, loc, cl_env)) in
      let () = loc := Some clo in
      (clo, bindloc (n, loc, env))
  | Exp e -> (evalexp e env, env)
```

The only tricky thing is the `match`, which *should* be unnecessary --- we know
that there's only one way a `Lambda` can evaluate: to a `Closure`! But OCaml's
type system doesn't know that. In order to make it understand, we'd need to
convert our types to polymorphic variants. [^polymorphic-variants] Only then
could we write something like:

```ocaml
[...]
let `Closure (formals, body, cl_env) = evalexp (`Lambda (ns, e)) env in
[...]
```

And avoid that whole headache. Perhaps we'll convert later.

In any case, that's it. That's lambdas, closures, define --- all taken care of.
If you're left wondering how we're supposed to have the `env` special form now
that we've completely changed how our environments work, swell. All the more
reason to download the code [here]({{ page.codelink }}) and mess with it.

Next up, [primitives III](/blog/lisp/11_prim3/).

[^refs]:
    `ref`s are probably not implemented as you'd expect. Since fields of OCaml
    records are the only things in the language that can be declared mutable with
    the `mut` keyword, a `ref` is a record with just one field: `contents`.
    Declared mutable, of course.

[^option-type]:
    [Here](http://ocaml-lib.sourceforge.net/doc/Option.html) is the API for OCaml's
    `option` type, and also some [reading](https://wiki.haskell.org/Maybe) on
    Haskell's `Maybe` monad.

[^polymorphic-variants]:
    Polymorphic variants are fun because they solve a bunch of nasty little type
    problems we have. They're also not fun because they're probably unfamiliar to
    OCaml newcomers, and require typing a bunch of backticks everywhere.
    [Here](http://stackoverflow.com/questions/9367181/variants-or-polymorphic-variants)'s
    an interesting StackOverflow question about them if you want to read some more.

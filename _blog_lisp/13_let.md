---
title: "Writing a Lisp, Part 13: Let"
author: Maxwell Bernstein
date: Mar 4, 2017
codelink: /resources/lisp/13_let.ml
---

Here we are, here we are, with a fully-functional programming language. And
yet, aside from using `val`, we have no good way of defining variables.
Additionally, `val` has the side-effect of mutating its current environment --
something we would rather avoid (at least for now).

Most functional languages (SML, OCaml, Haskell, every single Lisp) have a
solution to this problem: they have a form of expression called `let` that is
used to bind a name to a value *only* in a given expression. Here's what it
looks like in two different languages:

```ocaml
(* OCaml *)
let x = 4 in
  let y = 5 in
    y * x
```

```common-lisp
; Lisp
(let ((x 4) (y 5))
  (* y x))
```

Let, as it turns out, is a supremely important construct in functional
programming. Without it, the programmer is required to use imperative
constructs like `begin`, which evaluates a bunch of expressions and then
returns the value of the last one:

```scheme
(begin
  (val x 4)
  (val y 5)
  (* y x))
```

There's a problem with that, though: it pollutes the existing environment with
the values of `x` and `y`.  If you're feeling clever, you could instead use
`lambda`, like so:


```common-lisp
(((lambda (x)
     (lambda (y)
        (* y x))) 3) 4)
```

or even:

```common-lisp
((lambda (x y) (* y x))
  3 4)
```

If you guessed that using lambda-expressions to model `let` was a good idea,
you're sort of right!

You're right because it certainly works, and is more elegant than `begin`.  You
should have concerns, though, about the amount of stack space that your typical
lambda-expression uses. Right now, every single lambda-expression captures a
copy of the existing environment. That's a *lot* of wasted space ---
let-expressions are used often.

Another thing you should be wondering about the choice in implementation: is
there a difference between the two lambda representations above?

Not really, even considering exceptional cases like two variables with the same
name. Let's have a look:

```
$ ocaml 13_let.ml
> (((lambda (x) (lambda (x) x)) 5) 7)
7
> ((lambda (x x) x) 5 7)
7
> Exception: End_of_file.
$
```

Even though we don't check for duplicate variable names in lambda-expressions,
the results end up the same --- the inner (last) `x` is the one that is most
recently bound, and therefore used when evaluating the expression.

Either way, what we're going to do in our implementation is less clever ---
just add a new AST constructor and evaluate them differently from other types
of expression.

Yes, this is "lame" because it adds more features without "needing" to. But it
saves AST transform headache and gives some performance wins.

```ocaml
[...]
and exp =
  | Literal of value
  | Var of name
  | If of exp * exp * exp
  | And of exp * exp
  | Or of exp * exp
  | Apply of exp * exp
  | Call of exp * exp list
  | Lambda of name list * exp
  | Let of (name * exp) list * exp             (* NEW! *)
  | Defexp of def
```

Now of course we have to go and patch all the non-exhaustive warnings that come
up. In `string_exp`, we'll take the easy way out:

```ocaml
let rec string_exp =
  let spacesep_exp es = spacesep (List.map string_exp es) in
  function
  [...]
  | Let (bs, e) -> "#<let>"
  [...]
```

In `build_ast`, we have to do some "heavy lifting" in transforming the list of
lists to a `(name * exp) list`.

```ocaml
let rec build_ast sexp =
  [...]
  match sexp with
  [...]
  | Pair _ when is_list sexp ->
      (match pair_to_list sexp with
      [...]
      | (Symbol "let")::bindings::exp::[] when is_list bindings ->
          let mkbinding = function
            | Pair (Symbol n, Pair (e, Nil)) -> n, build_ast e
            | _ -> raise (TypeError "(let bindings exp)")
          in
          Let ((List.map mkbinding (pair_to_list bindings)), (build_ast exp))
      [...]
```

This, as it turns out, is a very convenient form for evaluation. We can take
the ASTs we've built, evaluate them, and then put them neatly into our current
environment when evaluating `exp`. Speaking of evaluation, now we'll handle the
`Let` case in `evalexp`:

```ocaml
let rec evalexp exp env =
  [...]
  let rec ev = function
    [...]
    | Let (bs, e) ->
        let mkbinding (n, e) = n, ref (Some (ev e)) in
        evalexp e (extend (List.map mkbinding bs) env)
    | Defexp d -> raise ThisCan'tHappenError
  in ev exp
```

The bit that's annoying is the `value option ref` type, so I've added
`mkbinding` (different from the one in `build_ast`) to simplify the code.

One thing you might notice is that we've done no checking for duplicate names.
That is, the following kind of expression is allowed:

```
$ ocaml 13_let.ml
> (let ((x 5) (x 7)) x)
5
> Exception: End_of_file.
$
```

Due to the way our environment is set up (toward the head of the linked list is
considered "more recent"), the binding from `x` to `5` takes over.

Since we've not done this kind of safety checking in the lambda-expressions or
in `define`, we're not going to do it here either; we can add that later if
need be.

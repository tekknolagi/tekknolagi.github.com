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
(* => 20 *)
```

```common-lisp
; Lisp
(let ((x 4) (y 5))
  (* y x))
; => 20
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
; => 20
```

There's a problem with that, though: it pollutes the existing environment with
the values of `x` and `y`. If `x` and `y` had other values beforehand... well,
sorry. They're gone now. If you're feeling clever, however, you could instead
use `lambda`, like so:


```common-lisp
(((lambda (x)
     (lambda (y)
        (* y x))) 3) 4)
; => 12
```

or even:

```common-lisp
((lambda (x y) (* y x))
  3 4)
; => 12
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

Yes, this is "lame" because it adds more core interpreter features without
"needing" to. But it saves some AST transform headache and gives some
performance wins. [^lowering]

```ocaml
[...]
and let_kind = LET | LETSTAR
and exp =
  | Literal of value
  | Var of name
  | If of exp * exp * exp
  | And of exp * exp
  | Or of exp * exp
  | Apply of exp * exp
  | Call of exp * exp list
  | Lambda of name list * exp
  | Let of let_kind * (name * exp) list * exp             (* NEW! *)
  | Defexp of def
```

The weird bit is the `let_kind`. Ignore `LETSTAR` for now --- I'll touch on it
toward the end of this post.

Now of course we have to go and patch all the non-exhaustive warnings that come
up.

In `string_exp`, we'll transform each binding into a string and then put them
all together:

```ocaml
let rec string_exp =
  let spacesep_exp es = spacesep (List.map string_exp es) in
  let string_of_binding (n, e) = "(" ^ n ^ " " ^ (string_exp e) ^ ")" in
  function
  [...]
  | Lambda (ns, e) ->  "(lambda (" ^ spacesep ns ^ ") " ^ string_exp e ^ ")"
  | Let (kind, bs, e) ->
      let str = match kind with | LET -> "let" | LETSTAR -> "let*" in
      let bindings = spacesep (List.map string_of_binding bs) in
      "(" ^ str ^ " (" ^ bindings ^ ") " ^ string_exp e ^ ")"
  [...]
```

I've also gone ahead and rewritten the stringify function for `lambda` to
actually print the whole expression.

In `build_ast`, we have to do some "heavy lifting" in transforming the list of
lists to a `(name * exp) list`.

```ocaml
exception UniqueError of string

let rec assert_unique = function
  | [] -> ()
  | x::xs -> if List.mem x xs then raise (UniqueError x) else assert_unique xs

let rec build_ast sexp =
  [...]
  let valid_let = function | "let" | "let*" -> true | _ -> false in
  let to_kind = function | "let" -> LET | "let*" -> LETSTAR
                         | _ -> raise (TypeError "Unknown let")
  in
  match sexp with
  [...]
  | Pair _ when is_list sexp ->
      (match pair_to_list sexp with
      [...]
      | (Symbol s)::bindings::exp::[] when is_list bindings && valid_let s ->
          let mkbinding = function
            | Pair (Symbol n, Pair (e, Nil)) -> n, build_ast e
            | _ -> raise (TypeError "(let bindings exp)")
          in
          let bindings = List.map mkbinding (pair_to_list bindings) in
          let () = assert_unique (List.map fst bindings) in
          Let (to_kind s, bindings, (build_ast exp))
      [...]
```

This code handles both `let` and `let*`, converting them into their appropriate
representations.

Note that I've also added a uniqueness constraint on the bindings -- each of
the names *must* be distinct from all others. This gets rid of some nastiness
that we looked at above. Since this is useful, I've also gone ahead and called
`assert_unique` in the `Lambda` and `Define` builders.

The resulting `Let` AST is, as it turns out, a very convenient form for
evaluation. We can take the ASTs we've built, evaluate them, and then put them
neatly into our current environment when evaluating `exp`. Speaking of
evaluation, now we'll handle the `Let` case in `evalexp`:

```ocaml
let rec evalexp exp env =
  [...]
  let rec ev = function
    [...]
    | Let (LET, bs, e) ->
        let evbinding (n, e) = n, ref (Some (ev e)) in
        evalexp e (extend (List.map evbinding bs) env)
    | Let (LETSTAR, bs, e) -> failwith "Not yet implemented"
    | Defexp d -> raise ThisCan'tHappenError
  in ev exp
```

The bit that's annoying is the `value option ref` type, so I've added
`evbinding` to simplify the code. I've also made sure that `let*` raises an
error because we haven't made it yet.

Let's give it a whirl to see if it works:

```
$ ocaml 13_let.ml
> (val x 4)
4
> (let ((x 2)) x)
2
> (let ((x 2) (y 4)) (* x y))
8
> (let ((x 3) (x 4)) x)
Exception: UniqueError "x".
$ ocaml 13_let.ml
> (let ((x 3) (y x)) y)
Exception: NotFound "x".
$ ocaml 13_let.ml
> (let* ((x 4) (y x)) y)
Exception: Failure "Not yet implemented".
$
```

Looks like our `UniqueError` works!

Even though I did not specify that this is correct, the expression `(let ((x 3)
(y x)) y)` *should* indeed be an error. This is because each binding expression
(`3` and `x` in this case) is evaluated in the pre-let environment, in
isolation.  Whenever I get around to formally specifying the behavior for this
language using *operational semantics*, this will become more clear.

But what if we want to refer to previous bindings? We can totally do that, and
that's where `let*` comes in. With `let*` each binding can refer to all
previous bindings, but no future ones. So it's kind of like a bunch of nested
`let`s:

```scheme
(let* ((x 5) (y x)) y)
; is equivalent to
(let ((x 5))
  (let ((y x))
    y))
```

We've already implemented all of the plumbing for `let*` except for evaluation.
Just like the implementation decision for `let`, we could simply do an AST
transform and make it a bunch of nested `let`. We're going to make a parallel
implementation instead:

```ocaml
let rec evalexp exp env =
  [...]
  let rec ev = function
    | Let (LET, bs, e) ->
        let evbinding (n, e) = n, ref (Some (ev e)) in
        evalexp e (extend (List.map evbinding bs) env)
    | Let (LETSTAR, bs, e) ->                           (* NEW! *)
        let evbinding acc (n, e) = bind(n, evalexp e acc, acc) in
        evalexp e (extend (List.fold_left evbinding [] bs) env)
    | Defexp d -> raise ThisCan'tHappenError
  in ev exp
```

With `fold_left`, we make sure that we evaluate each of the bindings in order,
and in the environment created by evaluating the previous bindings. Ahh,
functional programming... where would I be without you?

There's a third type of let-expression, `letrec`, that is like `let*` but it
allows a binding to reference any (even the current binding!) --- not just
bindings that precede it. I've not implemented `letrec` in this post. It's not
much more complicated (just some futzing with `bindloc` and `mkloc`).

Download the code [here]({{ page.codelink }}) if you want to mess with it.

I'm not entirely sure what's up next. We'll see!

<br />
<hr style="width: 100px;" />
<!-- Footnotes -->

[^lowering]:
    There's this idea of AST "lowering", which means taking a more advanced AST
    (like one with `let` in it) and transforming it into a less advanced AST
    (like one with all the `let`s transformed into `lambda`s). AST lowering is
    one of many common compiler stages in the process of producing machine
    code.

<!--
    http://tmcnab.github.io/Hyperglot/
-->

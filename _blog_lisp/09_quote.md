---
title: "Writing a Lisp, Part 9: Quote"
author: Maxwell Bernstein
date: Feb 1, 2017
codelink: /resources/lisp/09_quote.ml
layout: post
---

Welcome back! I promise this post won't be nearly as long as the post about
ASTs. This time we're going to cover the `quote` form and its special new
syntax.

Last time we noticed that writing something like:

```scheme
(apply + (list 1 2))
```

is not great, and we'd prefer to do instead:

```scheme
(apply + '(1 2))
```

and defer [RSI](https://en.wikipedia.org/wiki/Repetitive_strain_injury) as much
as we can. We'd also like to be able to have symbol literals, since right now
we can't even build those. Symbols are exclusively used for variable names and
there's no way to stop them from being evaluated as such:

```
$ ocaml 08_asts.ml
> (val x a)
Exception: NotFound "a".
$
```

So let's add something that can defer evaluation, whether it's for lists, for
variable names, or for entire expressions that we'll later feed into `eval`
(when we get to our metacircular evaluator). That something is `quote`.

According to the [Racket docs](https://docs.racket-lang.org/guide/quote.html):

> The `quote` form produces a constant:
>
> > ```scheme
> > (quote datum)
> > ```
>
> The syntax of a `datum` is technically specified as anything that the `read`
> function parses as a single element. The value of the `quote` form is the
> same value that `read` would produce given *`datum`*.

Which means, in short, that the result should be a `value`, not yet transformed
into an `exp`.

That sounds simple enough. Just... don't do anything. And it is! Let's add a
constructor for it in `lobject`:

```ocaml
type lobject =
  | Fixnum of int
  | Boolean of bool
  | Symbol of string
  | Nil
  | Pair of lobject * lobject
  | Primitive of string * (lobject list -> lobject)
  | Quote of value                                      (* NEW! *)
and value = lobject
```

We're adding it to `lobject` because our reader needs to be able to handle the
new syntax with the single quote, and the reader only returns `value`s -- not
`exp`s. Speaking of our reader, let's add just one single line to make this
work:

```ocaml
let rec read_sexp stm =
  [...]
  eat_whitespace stm;
  let c = read_char stm in
  [...]
  else if c = '\'' then Quote (read_sexp stm)            (* NEW! *)
  else raise (SyntaxError ("Unexpected char " ^ (stringOfChar c)));;
```

And there you have it. We can now read quoted expressions. The other form, the
one that looks like a function application, we'll handle as a special case
later --- outside of the reader.

We'll also want to add two cases to `build_ast` -- one for `'` and one for
`quote`:

```ocaml
let rec build_ast sexp =
  match sexp with
  | Primitive _ -> raise ThisCan'tHappenError
  | Fixnum _ | Boolean _ | Nil | Quote _ -> Literal sexp    (* NEW *)
  | Symbol s -> Var s
  | Pair _ when is_list sexp ->
      (match pair_to_list sexp with
      [...]
      | [Symbol "quote"; e] -> Literal (Quote e)            (* NEW *)
      | [Symbol "val"; Symbol n; e] -> Defexp (Val (n, build_ast e))
      | [Symbol "apply"; fnexp; args] ->
          Apply (build_ast fnexp, build_ast args)
      | fnexp::args -> Call (build_ast fnexp, List.map build_ast args)
      | [] -> raise (ParseError "poorly formed expression"))
  | Pair _ -> Literal sexp
```

The best part is... `eval` is the simplest bit! Just don't quote the expression
anymore and let it be evaluated!

```ocaml
let rec evalexp exp env =
  [...]
  let rec ev = function
    | Literal Quote e -> e                                  (* NEW *)
    | Literal l -> l
    | Var n -> lookup (n, env)
    [...]
  in ev exp
```

The only potentially tricky bit is that we have to add this case before the
more general `Literal l` case or OCaml will complain at us that the `Literal
Quote e` case is unused --- which it would be.

And we're done with `quote`. Just have to modify the printing function. Since
we're done so early, I'll also go ahead and take the liberty of re-working the
printing function into *stringifier*. That is, it won't actually do any
printing to the console, but instead return a string. This turns out to be more
flexible and easier to use in the long run. Since that's a pretty easy task ---
just remove all of the recursive `print_sexp` calls in favor of `string_sexp`
calls and concatenate the results together with `^` --- I'll also go ahead and
add an AST printer. Why not? It'll make debugging easier in the future anyway.

```ocaml
let rec string_exp = function
  | Literal e -> string_val e
  | Var n -> n
  | If (c, t, f) ->
      "(if " ^ string_exp c ^ " " ^ string_exp t ^ " " ^ string_exp f ^ ")"
  | And (c0, c1) -> "(and " ^ string_exp c0 ^ " " ^ string_exp c1 ^ ")"
  | Or (c0, c1) -> "(or " ^ string_exp c0 ^ " " ^ string_exp c1 ^ ")"
  | Apply (f, e) -> "(apply " ^ string_exp f ^ " " ^ string_exp e ^ ")"
  | Call (f, es) ->
      let string_es = (String.concat " " (List.map string_exp es)) in
      "(" ^ string_exp f ^ " " ^ string_es ^ ")"
  | Defexp (Val (n, e)) -> "(val " ^ n ^ " " ^ string_exp e ^ ")"
  | Defexp (Exp e) -> string_exp e

and string_val e =
    let rec string_list l =
        match l with
        | Pair (a, Nil) -> string_val a
        | Pair (a, b) -> string_val a ^ " " ^ string_list b
        | _ -> raise ThisCan'tHappenError
    in
    let string_pair p =
        match p with
        | Pair (a, b) -> string_val a ^ " . " ^ string_val b
        | _ -> raise ThisCan'tHappenError
    in
    match e with
    | Fixnum v -> string_of_int v
    | Boolean b -> if b then "#t" else "#f"
    | Symbol s -> s
    | Nil -> "nil"
    | Pair (a, b) ->
        "(" ^ (if is_list e then string_list e else string_pair e) ^ ")"
    | Primitive (name, _) -> "#<primitive:" ^ name ^ ">"
    | Quote v -> "'" ^ string_val v                         (* NEW *)
```

Note that these two functions are mutually recursive (like our types) and
therefore are defined with `and` and `let rec`.

Also note that this requires a small change to the `repl` function:

```ocaml
let rec repl stm env =
  print_string "> ";
  flush stdout;
  let ast = build_ast (read_sexp stm) in
  let (result, env') = eval ast env in
  print_endline (string_val result);
  repl stm env';;
```

and there we go:

```
$ ocaml 09-quote.ml
> 'a
a
> '4
4
> '(1 2 3)
(1 2 3)
> (apply + '(1 2))
3
> '(quote e)
(quote e)
> ''a
'a
> (val x 'x)
x
> x
x
> 'whaaaaaaaat?
whaaaaaaaat?
> Exception: End_of_file.
$
```

Download the code [here]({{ page.codelink }}) if you want to mess with it.

Next up, [closures](/blog/lisp/10_closures/).

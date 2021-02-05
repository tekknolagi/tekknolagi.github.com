---
title: "Writing a Lisp, Part 8: ASTs"
author: Maxwell Bernstein
date: Jan 31, 2017
codelink: /resources/lisp/08_asts.ml
layout: post
---

*Heads up: this will be a bit of a long post compared to previous posts. We've
got a lot of ground to cover.*

An Abstract Syntax Tree (AST) is a way of representing a program in a way that
is easy for another program to manipulate. It gets rid of all the text nonsense
and instead replaces it with datatypes from the programming language. For
example:

```scheme
(f 1 2 3)
```

might be represented by a tree that looks like:

```ocaml
Call(Var "f", [Fixnum 1; Fixnum 2; Fixnum 3])
```

which of course is longer, but it allows us to more easily

* enforce structural constraints on the program
* make use of OCaml's pattern matching without a bunch of auxiliary functions.

In this post, we're going to

1. add a new type to represent our AST
2. add a new step in our interpreter that takes an S-expression (like
   `Pair(Symbol "if", Pair(cond, Pair(iftrue, Pair(iffalse, Nil))))`) and turns
   it into an AST (like `If(cond, iftrue, iffalse)`)
3. rip out and slim down our `eval` function
4. and celebrate!

### Adding the AST type

Creating a new type is not hard. We've already got `lobject`, and we can reuse
that. I propose we add all the special forms as constructors for our new type:

```ocaml
type lobject =
  | Fixnum of int
  | Boolean of bool
  | Symbol of string
  | Nil
  | Pair of lobject * lobject
  | Primitive of string * (lobject list -> lobject)

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
  | Defexp of def

and def =
  | Val of name * exp
  | Exp of exp
```

The different types are recursively defined with `and`, which means they can
reference one another (as `exp` and `lobject`/`value` do).

I'm introducing `value` as an alias for `lobject` because really we won't be
evaluating raw `lobject`s anymore. Instead we'll use those as our
self-evaluating *values*. That is, they can be evaluated repeatedly and they
won't change.

I'm introducing `name` as an alias for `string` purely for the aesthetics --- I
think it makes the code easier to read. It doesn't change the type system at
all, since OCaml will accept `string` in place of `name` anywhere.

`exp` is a rather large type that has a bunch of constructors. I'll explain all
of them briefly:

* `Literal` holds any self-evaluating type. It's a container for `value`s.
* `Var` holds a variable name.
* `If` holds an if-expression --- condition, expression to be evaluated if
  true, expression to be evaluated if false.
* `And` and `Or` hold two conditions (expressions).
* `Apply` and `Call` are synonymous, for the most part. They call either
  primitive procedures or closures (coming soon) with the specified arguments
  (also called [actual parameters](https://chortle.ccsu.edu/java5/Notes/chap34A/ch34A_3.html)).
* `Defexp`s are kind of weird and more of a design choice than anything else.
  We have two classes of expression: those that modify the environment, and
  those that do not. Previously, this was implicit --- you had to check if the
  expression returned the original environment or a modified one in `eval`.
  Now, *only* `Defexp`s (currently just `val` --- but we'll add `define` soon)
  modify the environment. We'll find later that having `Exp` as a sub-case of
  `Defexp` makes our implementation much cleaner, and also allows us to have a
  ["last result"
  variable](http://stackoverflow.com/questions/9073273/variable-assigned-to-the-last-executed-line)
  in the REPL at no additional cost.

Okay, excellent. Now that we've modified an old type and added a couple of new
ones, it's time to put them to use. Our first step will be to write a function
that takes an S-expression from the reader and transforms it into an AST.

### Building an AST from S-expressions

Currently our REPL looks like this:

```ocaml
let rec repl stm env =
  print_string "> ";
  flush stdout;
  let sexp = read_sexp stm in
  let (result, env') = eval_sexp sexp env in
  print_sexp result;
  print_newline ();
  repl stm env';;
```

Let's add in the AST transform:

```ocaml
let rec repl stm env =
  print_string "> ";
  flush stdout;
  let ast = build_ast (read_sexp stm) in
  let (result, env') = eval ast env in
  print_val result;
  print_newline ();
  repl stm env';;
```

Note that I've also changed the name of `eval_sexp` to just `eval` and
`print_sexp` to `print_val`.

Now let's go ahead and write `build_ast`! We know that we need to generate all
of the different types of `exp`, so that will provide reasonable scaffolding
for our function. In general: numbers, bools, and empty lists should all
self-evaluate. Primitives and non-list pairs shouldn't happen at this stage in
the process. The others are special.

For example, a `Symbol` is probably a name, so should be a `Var`. Or there are
several types of list (like `(and #t #f)`) that are special. `Val` is even more
special in that it has to be wrapped in a `Defexp`. So there you have it:

```ocaml
exception ParseError of string

let rec build_ast sexp =
  match sexp with
  | Primitive _ -> raise ThisCan'tHappenError
  | Fixnum _ | Boolean _ | Nil -> Literal sexp
  | Symbol s -> Var s
  | Pair _ when is_list sexp ->
      (match pair_to_list sexp with
      | [Symbol "if"; cond; iftrue; iffalse] ->
          If (build_ast cond, build_ast iftrue, build_ast iffalse)
      | [Symbol "and"; c1; c2] -> And (build_ast c1, build_ast c2)
      | [Symbol "or"; c1; c2] -> Or (build_ast c1, build_ast c2)
      | [Symbol "val"; Symbol n; e] -> Defexp (Val (n, build_ast e))
      | [Symbol "apply"; fnexp; args] when is_list args ->
          Apply (build_ast fnexp, build_ast args)
      | fnexp::args -> Call (build_ast fnexp, List.map build_ast args)
      | [] -> raise (ParseError "poorly formed expression"))
  | Pair _ -> Literal sexp
```

I've also taken the liberty of introducing `and` and `or` as special forms.
Also, introducing a special `apply` form so it's easy to build lists of
arguments to functions programmatically, as in: `(apply + '(1 2))`. For now,
`Apply` and `Call` are separate forms that do roughly the same thing because I
can't think of an elegant way to make them work together with two different
types of list (the Lisp kind and OCaml kind).

This should all be reasonably self-explanatory. If it's not, take it apart.
Poke at it. Run it on some user input. See what it produces. Better yet, pause
here and try and write an AST printer (it'll be good for debugging anyway).

If you've either done that and come back, or decided that you understand that
enough, or decided that it'll probably make more sense as we continue ---
great! Let's keep on trucking.

### Rewriting the evaluator

So we've got these two classes of expression. One class changes the
environment and the other does not. I propose the following structure to manage
this:

```ocaml
let rec repl stm env =
  [...]
  let ast = build_ast (read_sexp stm) in
  let (result, env') = eval ast env in
  let () = print_endline (string_val result) in
  repl stm env'
```

Notice that we just have one `eval` --- `eval` can decide what kind of
expression the AST represents, then delegate to the right kind of function,
possibly mutating the environment in the process. Also notice I've swapped out
`print_sexp` for `string_of_value`; it's cleaner to transform to a string and
*then* use `print_endline` than to write your own print function.

Let's handle the delegating `eval` first:

```ocaml
let eval ast env =
  match ast with
  | Defexp d -> evaldef d env
  | e -> (evalexp e env, env)
```

Which is pretty clear. If the environment should change something, let it
(`evaldef` will return an environment).  Else, send it to `evalexp`, which just
returns a value, and return the original environment unmodified.

Speaking of, let's take a look at `evaldef`:

```ocaml
let evaldef def env =
  match def with
  | Val (n, e) -> let v = evalexp e env in (v, bind (n, v, env))
  | Exp e -> (evalexp e env, env)
```

Which looks pretty reasonable, right? If someone writes `(val x 5)` they should
definitely expect `x` to be bound in the new environment.

And this brings us to `evalexp`. We should have a case for each constructor in
`exp`, *including* `Defexp` --- but that case should just raise
`ThisCan'tHappenError`, since our program should never pass a `Defexp` to
`evalexp`.

```ocaml
let rec evalexp exp env =
  let evalapply f es =
    match f with
    | Primitive (_, f) -> f es
    | _ -> raise (TypeError "(apply prim '(args)) or (prim args)")
  in
  let rec ev = function
    | Literal l -> l
    | Var n -> lookup (n, env)
    | If (c, t, f) when ev c = Boolean true -> ev t
    | If (c, t, f) when ev c = Boolean false -> ev f
    | If _ -> raise (TypeError "(if bool e1 e2)")
    | And (c1, c2) ->
        begin
          match (ev c1, ev c2) with
          | (Boolean v1, Boolean v2) -> Boolean (v1 && v2)
          | _ -> raise (TypeError "(and bool bool)")
        end
    | Or (c1, c2) ->
        begin
          match (ev c1, ev c2) with
          | (Boolean v1, Boolean v2) -> Boolean (v1 || v2)
          | _ -> raise (TypeError "(or bool bool)")
        end
    | Apply (fn, e) -> evalapply (ev fn) (pair_to_list (ev e))
    | Call (Var "env", []) -> env
    | Call (e, es) -> evalapply (ev e) (List.map ev es)
    | Defexp d -> raise ThisCan'tHappenError
  in ev exp
```

There's some fun with guards when evaluating `If`. Guards allow us to reduce
the amount of `match` expressions. We could just as easily write this instead:

```ocaml
    [...]
    | If (c, t, f) ->
        begin
            match ev c with
            | Boolean true -> ev t
            | Boolean false -> ev f
            | _ -> raise (TypeError "(if bool e1 e2)")
        end
    [...]
```

And that is perhaps even *faster* than the alternative, unless OCaml has some
notion of function purity and automatically caches or does not recompute the
result of `ev c`. That would be neat.

Also note that `and` and `or` are not short-circuiting here, but rather
evaluate both expressions and then `&&` the resulting booleans. That's because
I'm lazy and don't want to write more code. In this language with no side
effects, ~~the only difference is in performance~~.

*EDIT: The difference is not only in performance. That's just plain wrong.
Consider the following example:*

```scheme
(or #t (if your-mom 3 4))
```

*With a short-circuting Lisp, there is no error. With an eagerly-evaluating
Lisp, there is an error -- since the condition must be a boolean expression,
and `your-mom` isn't even defined. No offense to your mother intended.*

Also note that I added in a case for `env`, which would have otherwise been
homeless in this new AST representation. Since `env` is more for debugging (at
least for now), I didn't think it was important enough to give it a
constructor.

Last, note that `Apply` and `Call` use the same function, `evalapply`, to
evaluate, so they are at least unified in the evaluation layer. Just not the
AST layer.

In any case... that's that! That's our evaluator. Let's give it a go:

```
$ ocaml 08_asts.ml
> (env)
((pair . #<primitive:pair>) (+ . #<primitive:+>) (list . #<primitive:list>))
> (+ 3 4)
7
> (and #t #f)
#f
> (and #t #t)
#t
> (or #f #f)
#f
> (or #f #t)
#t
> (if (and #t #f) 3 4)
4
> (if (or #t #f) 3 4)
3
> (val x 3)
3
> (env)
((x . 3) (pair . #<primitive:pair>) (+ . #<primitive:+>) (list . #<primitive:list>))
> (+ x 7)
10
> (apply pair (list 3 4))
(3 . 4)
> (pair 3 4)
(3 . 4)
> Exception: End_of_file.
$
```

Wait, what? What's that `list` function? Oh, I took the liberty of adding it
into the environment. Its primary purpose is to build lists easily:

```ocaml
let basis =
    let rec prim_list = function
        | [] -> Nil
        | car::cdr -> Pair(car, prim_list cdr)
    in
    [...]
    List.fold_left newprim Nil [
        ("list", prim_list);
        ("+", prim_plus);
        ("pair", prim_pair)
       ]
```

Download the code [here]({{ page.codelink }}) if you want to mess with it.

Next up, [quote](/blog/lisp/09_quote/).

---
title: "Writing a Lisp, Part 13: Let"
author: Maxwell Bernstein
date: Mar 14, 2017
codelink: /resources/lisp/13_let.ml
lispcodelink: /resources/lisp/13_let.lsp
layout: post
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
copy of the existing environment. That's a *lot* of wasted space [^improve] ---
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

### Let

Either way, what we're going to do in our implementation is less clever ---
just add a new AST constructor and evaluate them differently from other types
of expression.

Yes, this is "lame" because it adds more core interpreter features without
"needing" to. But it saves some AST transform headache and gives some
performance wins. [^lowering]

```ocaml
[...]
and let_kind = LET | LETSTAR | LETREC
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

The weird bit is the `let_kind`. Ignore `LETSTAR` and `LETREC` for now --- I'll
touch on them toward the end of this post.

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
      let str = match kind with
                | LET -> "let"
                | LETSTAR -> "let*"
                | LETREC -> "letrec"
      in
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
  let let_kinds = ["let", LET; "let*", LETSTAR; "letrec", LETREC] in
  let valid_let s = List.mem_assoc s let_kinds in
  let to_kind s = List.assoc s let_kinds in
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
          Let (to_kind s, bindings, build_ast exp)
      [...]
```

I set up `let_kinds` as an association list [^alist] and then make helper
functions (`valid_let`, to check if an symbol is "let", "let\*", or "letrec";
`to_kind` to transform a symbol into its equivalent constructor) to use down
below.

This code handles `let`, `let*`, and `letrec`, converting them into their
appropriate representations.

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
    | Let (LET, bs, body) ->
        let evbinding (n, e) = n, ref (Some (ev e)) in
        evalexp body (extend (List.map evbinding bs) env)
    | Let (LETSTAR, bs, body) -> failwith "Not yet implemented"
    | Let (LETREC, bs, body) -> failwith "Not yet implemented"
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
> (letrec () 'anything-here)
Exception: Failure "Not yet implemented".
$
```

Looks like our `UniqueError` works!

Even though I did not specify that this is correct, the expression `(let ((x 3)
(y x)) y)` *should* indeed be an error. This is because each binding expression
(`3` and `x` in this case) is evaluated in the pre-let environment, in
isolation.  Whenever I get around to formally specifying the behavior for this
language using *operational semantics*, this will become more clear.

### Letstar

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
    | Let (LET, bs, body) ->
        let evbinding (n, e) = n, ref (Some (ev e)) in
        evalexp body (extend (List.map evbinding bs) env)
    | Let (LETSTAR, bs, body) ->                           (* NEW! *)
        let evbinding acc (n, e) = bind (n, evalexp e acc, acc) in
        evalexp body (extend (List.fold_left evbinding [] bs) env)
    | Let (LETREC, bs, body) -> failwith "Not yet implemented"
    | Defexp d -> raise ThisCan'tHappenError
  in ev exp
```

*EDIT: Turns out there's a glaring issue in the `let*` implementation above.
See [16: Standard Library](../16_stdlib/) for corrections.*

With `fold_left`, we make sure that we evaluate each of the bindings in order,
and in the environment created by evaluating the previous bindings. Ahh,
functional programming... where would I be without you?

On another note, according to most of the implementations that I looked up when
writing this post, `let*` does not normally have a distinct variable
requirement, but lets later bindings shadow previous ones. This allows for
imperative-esque things like:

```scheme
; Not allowed in our interpreter:
(let* ((x 5)
       (x (factorial x))
       (x (sqrt x))
       (x (to-string x)))
  (print x))
```

Since we handled distinct names at the AST building level and treated all the
let-expressions as the same, ours will be a wee bit different. If you feel
compelled to change the interpreter so that binding uniqueness is checked at
evaluation-time instead of at AST-build-time, go right ahead. It shouldn't be
too much of a hassle or break any code we're going to write.

Let's test out `let*` now:

```
$ ocaml 13_let.ml
> (let* ((x 4) (y x)) y)
4
> Exception: End_of_file.
$
```

Swell.

### Letrec

The third type of let-expression, `letrec`, is like `let*` but it allows a
binding to reference any (even the current binding!) --- not just bindings that
precede it. The caveat, though, is that the values have to be
lambda-expressions. Otherwise we'd run into weird issues with topologically
sorting the variables so they can be properly recursively defined... bunch of
nonsense. It looks like this:

```scheme
(letrec ((f (lambda (x) (g (+ x 1))))
         (g (lambda (x) (+ x 3))))
  (f 0))
; => 4
```

or even:

```scheme
(letrec ((factorial (lambda (x)
                        (if (< x 2)
                           1
                           (* x (factorial (- x 1)))))))
  (factorial 5))
; => 120
```

It's what we *should* have used in the last post, instead of adding that ugly
hack to `evalapply`:

```ocaml
let rec evalexp exp env =
  let evalapply f vs =
    match f with
      | Primitive (_, f) -> f vs
      | Closure (ns, e, clenv) ->
          (*        vvvvvvv     ugly!! wrong!!     vvvv *)
          evalexp e (extend (bindlist ns vs clenv) env)
  [...]
```

So let's revert `evalapply` to its normal happy self:

```ocaml
let rec evalexp exp env =
  let evalapply f vs =
    match f with
    | Primitive (_, f) -> f vs
    | Closure (ns, e, clenv) -> evalexp e (bindlist ns vs clenv)
  [...]
```

<img class="post-inline-image" src="/assets/img/lisp/bob-ross.gif" />

...and get crackalackin' on `letrec`. First let's modify `mkloc` so that it can
take any argument and just returns a new empty ("unspecified") reference:

```ocaml
let mkloc () = ref None    (* OLD *)
let mkloc _ = ref None     (* NEW *)
```

We're not doing this to save source code space. That would be silly. We're
doing this so that we can more easily use it in mapping functions without
wrapping it in an anonymous function. You'll see what I mean in a second:

#### OCaml

```ocaml
let rec evalexp exp env =
  [...]
  let rec unzip ls = (List.map fst ls, List.map snd ls) in
  let rec ev = function
    [...]
    | Let (LETREC, bs, body) ->
        let names, values = unzip bs in
        let env' = bindloclist names (List.map mkloc values) env in
        let updates = List.map (fun (n, e) -> n, Some (evalexp e env')) bs in
        let () = List.iter (fun (n, v) -> (List.assoc n env') := v) updates in
        evalexp body env'
    | Defexp d -> raise ThisCan'tHappenError
  in ev exp
```

#### English

`unzip` is your standard unzip function. It takes a list of tuples and returns
two lists. The first list is all of the first elements and the second list is
all of the second elements. This implementation makes two passes over the list
but oh well. It's readable.

Here's how `letrec` works, in neatly enumerated steps [^nr]:

1. Use `unzip` to break apart the bindings given to `letrec` into lists of
   names and values.
2. Make a list of empty ("unspecified") locations by mapping `mkloc` over the
   values (this is just `List.map mkloc values`).
3. Bind the list of names to those empty values, attaching the resulting new
   environment to the current one.
4. Evaluate each of the expressions, making a neat little `option ref` box for
   them one by one.
5. Put all of those values from Step 4 into their proper boxes.
6. Evaluate the body of the `letrec` in the new environment with all of the
   values resolved.

At some point when I figure out a good way of making diagrams, or perhaps when
I find a good enough one online, I will include a diagram here. Call that a
TODO.

You're probably wondering why not do the let-expressions post first and *then*
do mutually recursive functions in the metacircular evaluator? Yeah, well. I
didn't think of that. That's part of the fun of this series, I think. I'm a
real human and I make mistakes. Why hide them?

I've fixed the metacircular evaluator now, and the results don't look too
shabby:

```scheme
(define eval. (e env)
   (letrec (
        (eval-cond. (lambda (c a)
            (cond ((null. c) 'error)
                  ((eval. (caar c) a)  (eval. (cadar c) a))
                  (#t (eval-cond. (cdr c) a)))))

        (map-eval. (lambda (exps env)
          (cond ((null. exps) '())
                (#t (cons (eval.  (car exps) env)
                          (map-eval. (cdr exps) env))))))
            )

      (cond
        ((sym? e) (lookup. e env))
        [...]
```

You can see we now define `eval-cond.` and `map-eval.` as lambda-expressions
inside a `letrec` in `eval.`, instead of as two mutually recursive auxiliary
functions. How wonderful!

Download the code [here (ml)]({{ page.codelink }}) and
[here (lisp)]({{ page.lispcodelink }}) if you want to mess with it.

Next up, [comments](/blog/lisp/14_comments/).

[^improve]:
    They use a lot of stack space *for now*. Later on we'll optimize
    lambda-expressions. I can hardly wait! (Seriously, it's awesome.)

[^lowering]:
    There's this idea of AST "lowering", which means taking a more advanced AST
    (like one with `let` in it) and transforming it into a less advanced AST
    (like one with all the `let`s transformed into `lambda`s). AST lowering is
    one of many common compiler stages in the process of producing machine
    code. There may even be multiple stages comprised completely of lowering
    transformations!

[^alist]:
    An association list is a linked list of key-value pairs. It can be used
    much the same way as a hash table can (query, add, remove, etc), except for
    the time complexity (`O(n)` vs a hashtable's `O(1)`). In this case, though,
    we've got an upper bound on the number of elements in our list --- 3. So
    technically still constant time!

[^nr]:
    A big thank you to [Norman Ramsey](http://www.cs.tufts.edu/~nr/), whose
    clear code and explanations helped fix a bug or two in this implementation.

<!--
    http://tmcnab.github.io/Hyperglot/
-->

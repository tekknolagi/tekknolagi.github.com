---
title: "Writing a Lisp, Part 18: No Define"
author: Maxwell Bernstein
date: May 11, 2017
codelink: /resources/lisp/18_nodefine.ml
layout: post
---

Okay so I lied. Since I'm studying abroad -- so not coding much -- and also
still trying to figure out how Menhir/Ocamlyacc and the gang work, I thought I
would write a short post about eliminating the special `define` form.

You might be asking yourself and/or me why I'm bothering to remove it, to which
I would answer: why not? Currently we have code duplication and special cases
for things that aren't actually special. And with this change we'll also save
13 precious lines of OCaml.

Remember the environment-location-binding dance that we did for `letrec`? Turns
out that's perfectly suitable for `define` as well. So let's rip out the
special logic for `define`.

We'll start by removing the AST constructor:

```diff 
diff --git a/17_modules.ml b/18_nodefine.ml
index fef7360..971eb0d 100644
--- a/17_modules.ml
+++ b/18_nodefine.ml
@@ -77,7 +77,6 @@ module Ast = struct
 
   and def =
     | Val of name * exp
-    | Def of name * name list * exp
     | Exp of exp
 
   let rec string_exp =
```

and the printer:

```diff
diff --git a/17_modules.ml b/18_nodefine.ml
index fef7360..971eb0d 100644
--- a/17_modules.ml
+++ b/18_nodefine.ml
@@ -99,8 +98,6 @@ module Ast = struct
         let bindings = spacesep (List.map string_of_binding bs) in
         "(" ^ s ^ " (" ^ bindings ^ ") " ^ string_exp e ^ ")"
     | Defexp (Val (n, e)) -> "(val " ^ n ^ " " ^ string_exp e ^ ")"
-    | Defexp (Def (n, ns, e)) ->
-        "(define " ^ n ^ "(" ^ spacesep ns ^ ") " ^ string_exp e ^ ")"
     | Defexp (Exp e) -> string_exp e
 
   and string_val = function
```

and the evaluator:

```diff
diff --git a/17_modules.ml b/18_nodefine.ml
index fef7360..971eb0d 100644
--- a/17_modules.ml
+++ b/18_nodefine.ml
@@ -378,16 +375,6 @@ module Evaluator : EVALUATOR = struct
   let evaldef def env =
     match def with
     | Val (n, e) -> let v = evalexp e env in (v, Env.bind (n, v, env))
-    | Def (n, ns, e) ->
-        let (formals, body, cl_env) =
-            (match evalexp (Lambda (ns, e)) env with
-             | Closure (fs, bod, env) -> (fs, bod, env)
-             | _ -> raise (TypeError "Expecting closure."))
-        in
-        let loc = Env.mkloc () in
-        let clo = Closure (formals, body, Env.bindloc (n, loc, cl_env)) in
-        let () = loc := Some clo in
-        (clo, Env.bindloc (n, loc, env))
     | Exp e -> (evalexp e env, env)
 
   let eval ast env =

```

and then finally we can substitute in the appropriate `letrec`. Since `define`
is just `letrec` of a `lambda` with a pretty face, we can transform it into a
`letrec`:

```diff
diff --git a/17_modules.ml b/18_nodefine.ml
index fef7360..971eb0d 100644
--- a/17_modules.ml
+++ b/18_nodefine.ml
@@ -285,9 +282,9 @@ module Reader : READER = struct
                                  (pair_to_list ns)
             in
             let () = assert_unique names in
-            Defexp (Def (n, names, build e))
-        | [Symbol "apply"; fnexp; args] ->
-            Apply (build fnexp, build args)
+            let lam = Lambda (names, build e) in
+            Defexp (Val (n, Let (LETREC, [(n, lam)], Var n)))
+        | [Symbol "apply"; fnexp; args] -> Apply (build fnexp, build args)
         | (Symbol "cond")::conditions -> cond_to_if conditions
         | (Symbol s)::bindings::exp::[] when is_list bindings && valid_let s ->
             let mkbinding = function
```

The above diff is kind of confusing so I will reproduce the new code here:

```ocaml
  let rec build sexp =
    [...]
    match sexp with
    [...]
    | Pair _ when is_list sexp ->
        (match pair_to_list sexp with
        [...]
        | [Symbol "define"; Symbol n; ns; e] ->
            let err () = raise (TypeError "(define name (formals) body)") in
            let names = List.map (function Symbol s -> s | _ -> err ())
                                 (pair_to_list ns)
            in
            let () = assert_unique names in
            let lam = Lambda (names, build e) in
            Defexp (Val (n, Let (LETREC, [(n, lam)], Var n)))
```

In essence:

1. Make sure the "shape" of the expression is alright
2. Get the list of string names
3. Make sure they are unique
4. Build a lambda expression
5. Build a letrec expression
6. Build a val expression

In Lisp:

```scheme
; This
(define fact (x)
  (if (< x 2)
      1
      (* x (fact (- x 1)))))

; is equivalent to this
(val fact (letrec ((fact (lambda (x)
                            (if (< x 2)
                                1
                                (* x (fact (- x 1)))))))
             fact))
```

Which, if you don't care to test it yourself, still works:

```
$ ocaml 18_nodefine.ml
> (define fact (x) (if (< x 2) 1 (* x (fact (- x 1)))))
#<closure>
> (fact 5)
120
> 
```

Download the code [here]({{ page.codelink }}) if you want to mess with it.

On a completely unrelated note, I came across [another blog][rein-lisp] *also*
called "Writing a Lisp", so I wrote the author an email saying his was cool ---
and then he mentioned this blog! So shout out to Rein van der Woerd from the
Netherlands!

In the next chapter, I *actually* plan on replacing the reader with a much
better-designed lexer/parser.

[rein-lisp]: http://web.archive.org/web/20180825200936/http://www.reinvanderwoerd.nl/blog/2017/04/21/writing-a-lisp-debugger

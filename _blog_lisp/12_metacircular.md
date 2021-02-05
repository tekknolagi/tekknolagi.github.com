---
title: "Writing a Lisp, Part 12: Metacircular Evaluator"
author: Maxwell Bernstein
date: Mar 1, 2017
codelink: /resources/lisp/12_metacircular.ml
lispcodelink: /resources/lisp/12_metacircular.lsp
layout: post
---

<!--
http://inst.eecs.berkeley.edu/~cs61a/su10/resources/sp11-Jordy/mce.html
http://ep.yimg.com/ty/cdn/paulgraham/jmc.lisp
http://languagelog.ldc.upenn.edu/myl/ldc/llog/jmc.pdf
https://www.cs.bham.ac.uk/research/projects/poplog/paradigms_lectures/lecture18.html
https://news.ycombinator.com/item?id=8714988
https://github.com/pyrocat101/opal
-->

The term "metacircular evaluator" is a rather opaque way of saying "Lisp
written in the Lisp you just wrote", which is what we're going to test out
right now.

It's a fun demonstration that you've build enough of an interpreter in OCaml
(we'll call it Lisp<sub>0</sub>) that you can build a shorter version of that
same language, and extend it from there (we'll call that Lisp<sub>1</sub>).

### Paul Graham's Lisp

I've taken [Paul Graham](http://www.paulgraham.com/)'s (henceforth written as
"pg") implementation of the metacircular evaluator and modified it slightly for
our use case. It's easy to read, and short enough to be understood fully in a
sitting. I'll walk through it function by function.

```scheme
(define null. (x)
  (eq x '()))
```

This seems to be a reasonable way to check if a thing is `nil` --- check
equality using the built-in `eq`.

```scheme
(define and. (x y)
  (cond (x (cond (y #t)
                 (#t #f)))
        (#t #f)))
```

pg's Lisp uses `cond` to write `and.`, but we ended up building that into
Lisp<sub>0</sub> as a special form.

```scheme
(define not. (x)
  (cond (x #f)
        (#t #t)))
```

pg's Lisp uses `cond` to invert the truthiness of `x`.

```scheme
(val cons pair)

(define append. (x y)
  (cond ((null. x) y)
        (#t (cons (car x)
                  (append. (cdr x) y)))))
```

In order to give `list` access to `pair`, we have to make an alias and call it
`cons`. This is the simple non-tail-recursive version of `append`.

```scheme
(define list. (x y)
  (cons x (cons y '())))
```

We could also just dispatch to the built-in `list` function instead of using
`cons`.

```scheme
(define pair. (x y)
  (cond ((and. (null. x) (null. y)) '())
        ((and. (not. (atom? x)) (not. (atom? y)))
         (cons (list. (car x) (car y))
               (pair. (cdr x) (cdr y))))))
```

Confusingly enough, pg's Lisp defines a function named `pair.` that zips two
lists together, like so:

```
> (pair. '(1 2 3) '(a b c))
((1 a) (2 b) (3 c))
```

Note that the elements of the parent list are themselves lists, not just pairs.

I think it'll be easier if we rename `pair.` to `zip.`.

```scheme
(define o (f g) (lambda (x) (f (g x))))
(val caar (o car car))
(val cadar (o car (o cdr car)))
```

pg's Lisp requires some functions like `caar` and `caddr`, that are defined to
be `(car (car x))` and `(car (cdr (cdr x)))`, respectively. I've added in a
function called `o` that does function composition, so that we can support
those easily.

```scheme
(define lookup. (key alist)
  (cond ((eq (caar alist) key) (cadar alist))
        (#t (lookup. key (cdr alist)))))
```

`lookup.` looks up a variable in an environment. pg calls it `assoc.`, but we
might as well call it `lookup.`, since that's what we use internally (in
OCaml). His environment uses a list of lists instead of a list of pairs and
(due to our implementation of `lookup.`) returns `error` if `key` is not found
in `alist`.

### `eval`

The key to the interpreter is the `eval` function. It's definitely rather long
for a Lisp function, so I'm going to walk through it in comments. The only
thing, though, is that our Lisp reader does not understand comments --- so
you can't just copy and paste it into the Lisp<sub>0</sub> shell.

```scheme
; eval takes two parameters: an expression and an environment. It's like our
; evalexp.
(define eval. (e env)
  ; There are a lot of cases to consider. This is like our large match
  ; expression.
  (cond
    ; If it's a symbol, look it up. This is different from pg's Lisp in that
    ; he *only* has symbols to work with.
    ((sym? e) (lookup. e env))
    ; If it's some other type of atom, just leave it be. Let it self-evaluate.
    ((atom? e) e)
    ; If it's a list (the only alternative to being an atom), check if the
    ; first item is an atom.
    ((atom? (car e))
     ; What kind of form is it?
     (cond
       ; Quote accepts one argument, so just return that argument as an
       ; unevaluated expression (note the lack of a recursive call to eval.).
       ((eq (car e) 'quote) (cadr e))
       ; For atom?, eq, car, cdr, and cons, just evaluate the expression then
       ; pass it through to the built-in form.
       ((eq (car e) 'atom?) (atom? (eval. (cadr e)  env)))
       ((eq (car e) 'eq)    (eq    (eval. (cadr e)  env)
                                   (eval. (caddr e) env)))
       ((eq (car e) 'car)   (car   (eval. (cadr e)  env)))
       ((eq (car e) 'cdr)   (cdr   (eval. (cadr e)  env)))
       ((eq (car e) 'cons)  (cons  (eval. (cadr e)  env)
                                   (eval. (caddr e) env)))
       ; For cond, it's a wee bit tricker. We get to this function a bit later.
       ((eq (car e) 'cond)  (eval-cond. (cdr e) env))
       ; ...else, try and evaluate the function as a user-defined function,
       ; applying it to the arguments.
       (#t (eval. (cons (lookup. (car e) env)
                        (cdr e))
                  env))))
    ; If it's a compound expression in which the first element is a
    ; label-expression,
    ((eq (caar e) 'label)
     ; ...evaluate the expression in environment with a new recursive binding.
     (eval. (cons (caddar e) (cdr e))
            (cons (list. (cadar e) (car e)) env)))
    ; If it's a compound expression in which the first element is a
    ; lambda-expresison,
    ((eq (caar e) 'lambda)
     ; ...evaluate the application of the lambda to the given arguments,
     ; evaluating them.
     (eval. (caddar e)
            (append. (zip. (cadar e)
                           (map-eval. (cdr e) env))
                     env)))))

; Some helpers...

; cond works by evaluating each of the conditions in order until it encounters
; a truthy one.
(define eval-cond. (c env)
  ; If we have no more conditions left, there's an error.
  (cond ((null. c) 'error)
         ; If the current condition is true, evaluate that branch.
         (eval. (caar c) env)   (eval. (cadar c) env))
         ; Otherwise, keep going.
         (#t (eval-cond. (cdr c) env))))

; This is a manually curried form of map. It runs eval over every element in a
; list using the given environment.
(define map-eval. (exps env)
  (cond ((null. exps) '())
        (#t (cons (eval.  (car exps) env)
                  (map-eval. (cdr exps) env)))))
```

And there we have it! Now, it took me a bit to understand exactly *how* to use
this so-called "metacircular evaluator". It's not obviously well-documented.
However, there is an [excellent pdf](http://languagelog.ldc.upenn.edu/myl/ldc/llog/jmc.pdf)
that helped me understand the finer points of `label`.

`label` is used much like our built-in `define`, except that it requires manual
use of `lambda`. Here's what I mean:

```scheme
; Our Lisp
(define fact (x)
  (cond ((< x 2) 1)
         (#t (* x (fact (- x 1))))))

; pg's Lisp
(label fact
  (lambda (x)
    (cond ((< x 2) 1)
           (#t (* x (fact (- x 1)))))))
```

While we could build in the nice syntactic transform that adds the `lambda` for
us, effectively turning `label` into `define`, that's apparently too much
effort. Oh well!

In any case, let's try running this thing. A couple of notes:

* We don't have a `load` function yet, so there's no easy way to bring this in
  as a sort of library.
* It turns out that it's easier with our setup to add a `sym?` primitive and
  use actual booleans instead of the symbol `t`.
* We haven't given Lisp<sub>1</sub> any access to math primitives, so we'll
  need to patch those through to make an example like factorial work.

So I've added the pass-throughs for `+`, `*`, `-`, and `<` so we can write
factorial. Here's what it looks like:

```
$ cat 12_metacircular.lsp
[...]
(eval. '((label fact
                (lambda (x)
                  (cond ((< x 2) 1)
                        (#t (* x (fact (- x 1)))))))
         5)
       '())
$ ocaml 12_metacircular.ml < 12_metacircular.lsp
> #<closure>
> #<closure>
> #<closure>
> #<closure>
> #<primitive:pair>
> #<closure>
> #<closure>
> #<closure>
> #<closure>
> #<closure>
> #<closure>
> #<closure>
> #<closure>
> #<closure>
> #<closure>
> #<closure>
> #<closure>
> 120
> Exception: End_of_file.
$
```

Yeah, that looks weird. Turns out `eval.` can only evaluate one expression at a
time. Understandable. So `label` is less of a `define` and more of a "recursive
lambda". It needs to be applied immediately --- there's no `val`, there's no
`define`, and it can't stand alone and evaluate into a closure.

Adding a `begin` statement that allows for imperative programs would be another
feature, but we've chosen not to include it for simplicity's sake.

Download the code [here (ml)]({{ page.codelink }}) and
[here (lisp)]({{ page.lispcodelink }}) if you want to mess with it.

And there you have it --- reasonable proof that our Lisp implemementation can
be considered feature complete. Now we can move on to features that make the
language easier to use, like [let](/blog/lisp/13_let/).

---
title: "Writing a Lisp, Part 11: Primitives III"
codelink: /resources/lisp/11_prim3.ml
date: Feb 8, 2017
---

In order to have a complete Lisp, to be able to write a metacircular evaluator,
we need to add a couple more primitives, and perhaps also refine the way we add
primitives to our basis.

According to [this StackOverflow answer](http://stackoverflow.com/a/3484206),
the required functions for building a Lisp are:

1. `atom?`
2. `car`
3. `cdr`
4. `cons`
5. `quote`
6. `cond`/`if`
7. `lambda`
8. `label`/`define`

Although with some [fun mathy things](http://mvanier.livejournal.com/2897.html)
you can do away with `define`.

This proposed Lisp does not have numbers, though they could reasonably easily
be implemented using [Church numerals](https://en.wikipedia.org/wiki/Church_encoding#Church_numerals). We've
opted to make our lives a wee bit easier and our Lisp quite a bit faster by
instead using OCaml's built-in integer type.

The proposed Lisp *also* does not have the boolean type, and that is because
the symbol/atom `t` is considered "true", and everything else "false".

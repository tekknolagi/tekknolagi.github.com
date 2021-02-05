---
title: "Writing a Lisp, Part 17: Modules"
author: Maxwell Bernstein
date: May 2, 2017
codelink: /resources/lisp/17_modules.ml
layout: post
---

Right now we have a fully functional (in the general sense, not the math/CS
sense) Lisp, ready-to-go. Where could we possibly take it from here?

Well for one, I'd like to replace the relatively crappy hacked-together parser
with something nicer, like a parser generator. This post certainly won't do
that, or even explain what that means, but it will pave the way toward
swappable parsers.

Since all of our code right now is rather intertwined with a complicated
dependency graph, there's no simple way to, say, drop in a replacement parser.
If we were to separate out all the responsibilities and concerns into their
separate modules, this problem would be significantly reduced.

Without modifying much code (really only function names, etc), I created the
following modules:

```ocaml
module Env : sig
  type 'a env = (string * 'a option ref) list
  exception NotFound of string
  exception UnspecifiedValue of string
  val mkloc : 'a -> 'b option ref
  val bind : string * 'a * 'a env -> 'a env
  val bindloc : string * 'a option ref * 'a env -> 'a env
  val bindlist : string list -> 'a list -> 'a env -> 'a env
  val bindloclist : string list -> 'a option ref list -> 'a env -> 'a env
  val lookup : string * 'a env -> 'a
  val extend : 'a env -> 'a env -> 'a env
end = struct
  (* ... *)
end

module Ast = struct
  (* ... *)
end

module PushbackReader : sig
  type 'a t

  val of_string : string -> char t
  val of_channel : in_channel -> char t

  val do_stdin : 'a t -> 'b -> ('b -> unit) -> unit
  val read_char : char t -> char
  val unread_char : char t -> char -> char t
end = struct
  (* ... *)
end

module type READER = sig
  val read_exp : char PushbackReader.t -> Ast.exp
end

module type EVALUATOR = sig
  val eval : Ast.exp -> Ast.value Env.env -> Ast.value * Ast.value Env.env
end

module Reader : READER = struct
  (* ... *)
end

module Eval : EVALUATOR = struct
  (* ... *)
end
```

The only "big" change I made was to bring AST building *into* the Reader
module, and change the name of `read_sexp` to `read_exp` --- because now it
really reads expressions.

For people not super familiar with OCaml modules, let's chat about these
modules a bit before continuing.

Modules are OCaml's way of separating functionality into reusable containers.
Most modules contain a data type and a set of functions for operating on that
data type.

Modules by default expose all that they contain to the outside world. If you
would like to constrain what users of the modules can see (so that they need
not concern themselves with helper functions, etc), you can provide an
*interface* to the module. In our case, we're doing that with `sig`.

I have deliberately restricted the available functionality of `Reader` and
`Evaluator` to the `module type`s (interfaces) `READER` and `EVALUATOR`,
respectively. This enables us to swap out those modules with *any other*
modules so long as they conform to the right interfaces.

Download the code [here]({{ page.codelink }}) if you want to mess with it.

In the [next chapter](/blog/lisp/18_nodefine/), I ~~plan on
replacing the reader with a much better-designed lexer/parser. An
auto-generated one, even.~~ do some
syntax transforms to remove `define`.

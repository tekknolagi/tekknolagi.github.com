---
title: "Writing a Lisp, Part 14: Comments"
author: Maxwell Bernstein
date: Mar 15, 2017
codelink: /resources/lisp/14_comments.ml
lispcodelink: /resources/lisp/14_comments.lsp
layout: post
---

It would be really nice if we could document our code in line. Right? I mean,
look back at the [metacircular evaluator](../12_metacircular/). It had comments
that made the functions so much clearer... but we couldn't actually use that
code because our interpreter has no idea what a semicolon is. Turns out this is
really easy to add to our reader, so this post will be fairly short.

All we *really* need to do to add line comments is add the ability to say "if
we see a semicolon, ignore the rest of the line". That means we can do things
like this:

```
$ ocaml 14_comments.ml
> ((lambda ; this is a lambda
(x) (+ x 1)) 4)
5
> Exception: End_of_file.
$
```

While that comment is not at all useful, it *does* get ignored when evaluating
the expression, which is what we want. But enough fluffing about. The
implementation is a grand total of 4 lines:

```ocaml
let rec read_sexp stm =
  [...]
  let rec eat_comment stm =                          (* NEW *)
    if (read_char stm) = '\n' then () else eat_comment stm
  in
  eat_whitespace stm;
  let c = read_char stm in
  if c = ';' then (eat_comment stm; read_sexp stm)   (* NEW *)
  else if is_symstartchar c
  [...]
```

And the code reads pretty much the same as the English. If we wanted to, we
could pre-emptively eat comments like we do whitespace, or we could have a
separate preprocessing step entirely --- but this is fine for now.

Since we now support comments, I've transferred the comments from the
metacircular evaluator post into the new-and-improved version with `letrec`!
The results can be found [here (lisp)]({{ page.lispcodelink }}).

Download the code [here (ml)]({{ page.codelink }}) if you want to mess with it.

Next up, [printing](/blog/lisp/15_io/). How else are we going to
debug our code? /s

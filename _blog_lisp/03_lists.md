---
title: "Writing a Lisp, Part 3: Lists"
author: Maxwell Bernstein
date: Dec 6, 2016
codelink: /resources/lisp/03_lists.ml
layout: post
---

Last time we added symbols to our interpreter, so that leaves us currently with
symbols, integers, and booleans. That's great and all, but isn't the whole
point of Lisp to have lists? 'bout time we added those.

<!--
In Lisp you can reason about two types of lists: normal lists, which get
evaluated, and quoted lists, which are (for the time being) data, and do not
get evaluated.

```scheme
(add 3 4)     ;; Going to get evaluated!
'(42 69 613)  ;; Going to get stored as data!
```

The difference in syntax is just one little single quote, but they are very
different. The former is a function call, while the latter is just a collection
of elements. The fun part about Lisp: they're stored nearly identically.
-->

The fun part about Lisp: lists don't technically exist. Instead what we
have are called [cons cells](https://en.wikipedia.org/wiki/Cons):

<figure style="display: block; margin: 0 auto; max-width: 450px;" >
  <img class="svg" style="max-width: 450px;" src="/assets/img/lisp/03_lists_cons.svg" />
  <figcaption>Fig. 1 - Cons cell diagram of our above list, courtesy of
              Wikipedia.</figcaption>
</figure>

That's a fancy way to say that the only [compound data
type](https://en.wikipedia.org/wiki/Composite_data_type) in our language is a
[pair](https://docs.racket-lang.org/guide/pairs.html), and lists are pairs all
the way down. If you had a function called `pair` that constructed a pair out
of its two arguments, it would look like this:

```scheme
(pair 42 (pair 69 (pair 613 nil)))
```

Where `nil` is the `NULL` value of our language. All lists in Lisp end with
`nil`, just as all C-style strings end with the 0 byte (`NUL`). Since all lists
end in `nil`, any pair construct that *doesn't* end in a list is just a
handy-dandy pair:

```scheme
(pair 'Mozart 'Marriage-Of-Figaro)  ;; Some contrived example
```

Anyway. Before we get started parsing (which is easier than you probably
think), let's talk type representation. Currently we've got `Fixnum`,
`Boolean`, and `Symbol` type constructors:

```ocaml
type lobject =
  | Fixnum of int
  | Boolean of bool
  | Symbol of string
```

Let's add `nil` as its own type, since it's a special value. Then we can just
represent a pair as an OCaml tuple of any combination of `lobject`s!

```ocaml
type lobject =
  | Fixnum of int
  | Boolean of bool
  | Symbol of string
  | Nil
  | Pair of lobject * lobject
```
This means that we've just knocked off `nil`, pairs, and lists all in one go.
In two lines. Bam. OCaml. So what does a list look like in our `lobject` type
system?

```ocaml
Pair(Fixnum 42, Pair(Fixnum 69, Pair(Fixnum 613, Nil))) (* Gross *)
```

Okay, so that's not so great to type manually. I'll give you that one. But
thankfully we won't have to type it manually too much -- the parser should
build it for us! And, if we really want, we could write a function to transform
that into a native OCaml `lobject list` for us. Maybe something like:

```ocaml
(* TODO: Test. Currently Untested. *)
let rec pair_to_list pr =
  match pr with
    | Nil -> []
    | Pair(a, b) -> a::(pair_to_list b)
```

The only problem with that function is that it will raise an exception if it
encounters an argument of type "something else than `Nil` or `Pair`".

In any case, the `Pair` representation is just fine. Let's try some parsing!

```ocaml
let rec read_object stm =
  [...]
  let rec read_list stm =              (* NEW *)
    eat_whitespace stm;
    let c = read_char stm in
    if c = ')'
    then Nil
    else
      let _ = unread_char stm c in
      let car = read_sexp stm in
      let cdr = read_list stm in
      Pair(car, cdr)
  [...]
  in
  eat_whitespace stm;                  (* OLD stuff *)
  let c = read_char stm in
  [...]
  else if c = '('
  then read_list stm                   (* NEW *)
  [...]
  else raise (SyntaxError ("Unexpected char " ^ Char.escaped c));;
```

Let's pick apart the two new things here. First (in execution order), we have a
clause in our parser that detects if we're on an opening parenthesis. If we
are, it's time to read a list. So read it!

Next, we have a function `read_list` that happily crunches in new data until it
sees a closing parenthesis. The fun part is, it's potentially recursive in how
it handles lists! If the input is `(1 2 (3 4))`, for example, it'll start to
read in `(3 4)`, notice that there is a left parenthesis, and then read in a
new list! And return it! To `read_object`! Recursion is super cool.

Sweet. So now it's technically possible to read in lists in the REPL but we
won't actually be able to visually confirm that unless we can show some
evidence in the Print part of the REPL -- which means that we need to add to
`print_sexp`. Let's work out a skeleton:

```ocaml
let rec print_sexp e =
    match e with
    | Fixnum(v) -> print_int v
    | Boolean(b) -> print_string (if b then "#t" else "#f")
    | Symbol(s) -> print_string s
    | Nil -> (* ??? *)
    | Pair(a, b) -> (* ??? *)
```

The `Nil` case is pretty simple for us, thankfully. We can just print the
string `"nil"` and that's that. Pair is a little bit more complicated in that
it has two things to print and has to deal with parentheses. But let's give it
a go anyway.

```ocaml
let rec print_sexp e =
    match e with
    | Fixnum(v) -> print_int v
    | Boolean(b) -> print_string (if b then "#t" else "#f")
    | Symbol(s) -> print_string s
    | Nil -> print_string "nil"
    | Pair(a, b) ->
            print_string "(";
            print_sexp a;
            print_string " . ";
            print_sexp b;
            print_string ")";;
```

Whew. So just recursion, once more. Print parens, print the contained objects,
get on with your life. Let's give it a whirl!

```
$ ocaml 03_lists.ml
File "03_lists.ml", line 47, characters 4-73:           # from pair_to_list
Warning 8: this pattern-matching is not exhaustive.
Here is an example of a value that is not matched:
(Fixnum _|Boolean _|Symbol _)
> (42 69 613)
(42 . (69 . (613 . nil)))
>
```

Hey, that's pretty similar to both Wikipedia's diagram, the `pair` function
calls, and our own internal representation with `Pair`! Neat. It's not
generally what we want when examining lists, though. It's much easier for the
(very much human) programmer to look at the flat representation of nested
pairs.

In order to distinguish between "normal" pairs and pairs that are lists, we're
going to have to make a guess and have a function called `is_list`. Then we can
write a function to print that type of flat list, and a function to print a
"normal" pair.

```ocaml
[...]
let rec print_list l =
    match l with
    | Pair(a, Nil) -> print_sexp a;
    | Pair(a, b) -> print_sexp a; print_string " "; print_list b
in
let print_pair (Pair(a, b)) =
    print_sexp a; print_string " . "; print_sexp b
in
[...]
```

Fine, sure -- but how do we write `is_list`? I suppose if every second element
all the way down is a list and at some point it ends in `nil`, then it's a
list...

```ocaml
[...]
let rec is_list e =
    match e with
    | Nil -> true
    | Pair(a, b) -> is_list b
    | _ -> false
in
[...]
```

Great! So, all-in-all, our print function looks like this:

```ocaml
let rec print_sexp e =
    let rec is_list e =
        match e with
        | Nil -> true
        | Pair(a, b) -> is_list b
        | _ -> false
    in
    let rec print_list l =
        match l with
        | Pair(a, Nil) -> print_sexp a
        | Pair(a, b) -> print_sexp a; print_string " "; print_list b
    in
    let print_pair p =
        match p with
        | Pair(a, b) -> print_sexp a; print_string " . "; print_sexp b
    in
    match e with
    | Fixnum(v) -> print_int v
    | Boolean(b) -> print_string (if b then "#t" else "#f")
    | Symbol(s) -> print_string s
    | Nil -> print_string "nil"
    | Pair(a, b) ->
            print_string "(";
            if is_list e
            then print_list e
            else print_pair e;
            print_string ")";;
```

Doesn't look so bad. So let's try it out.

```
$ ocaml 03_lists.ml
File "03_lists.ml", line 48, characters 4-73:
Warning 8: this pattern-matching is not exhaustive.
Here is an example of a value that is not matched:
(Fixnum _|Boolean _|Symbol _)
File "03_lists.ml", line 116, characters 8-129:
Warning 8: this pattern-matching is not exhaustive.
Here is an example of a value that is not matched:
(Fixnum _|Boolean _|Symbol _|Nil)
File "03_lists.ml", line 121, characters 19-88:
Warning 8: this pattern-matching is not exhaustive.
Here is an example of a value that is not matched:
(Fixnum _|Boolean _|Symbol _|Nil)
> (42 69 613)
(42 69 613)
>
```

Not great. We're writing these functions everywhere that assume *so many
things* about the types of their arguments that just can't be assumed. Like
what happens if we give `print_list` a `Fixnum`? Bad, bad things. So let's
patch those up with a new exception. Let's called it `ThisCan'tHappenError`:

```ocaml
exception ThisCan'tHappenError;;
```

and let's make sure to put one in `pair_to_list`:

```ocaml
let rec pair_to_list pr =
match pr with
| Nil -> []
| Pair(a, b) -> a::(pair_to_list b)
| _ -> raise ThisCan'tHappenError;;
```

and in `print_list`:

```ocaml
[...]
let rec print_list l =
    match l with
    | Pair(a, Nil) -> print_sexp a
    | Pair(a, b) -> print_sexp a; print_string " "; print_list b
    | _ -> raise ThisCan'tHappenError
in
[...]
```

and in `print_pair`:

```ocaml
[...]
let print_pair p =
    match p with
    | Pair(a, b) -> print_sexp a; print_string " . "; print_sexp b
    | _ -> raise ThisCan'tHappenError
in
[...]
```

and try one more time.

```
$ ocaml 03_lists.ml
> (42 69 613)
(42 69 613)
> ()
nil
> nil
nil
>
```

Ahhhhhh. Much better.

But you might be asking yourself (or me) -- how can we possibly make a non-list
`Pair` from the REPL if the REPL will only read lists? Great question. The
answer for now is that we'll have to add a special function later that builds
pairs for us. It shouldn't be a great need until then.

Download the code <a href="{{ page.codelink }}">here</a> if you want to mess
with it.

Next up, [environments](/blog/lisp/04_environments/).

---
title: "Compiling a Lisp: Let"
layout: post
date: 2020-09-20 10:00:00 PDT
---

*[first]({% link _posts/2020-08-29-compiling-a-lisp-0.md %})* -- *[previous]({% link _posts/2020-09-21-compiling-a-lisp-6.md %})*

Welcome back to the "Compiling a Lisp" series. Last time we added a reader
(also known as a parser) to our compiler. This time we're going to compile a
new form: *let* expressions.

Let expressions are a way to bind variables to values in a particular scope.
For example:

```common-lisp
(let ((a 1) (b 2))
  (+ a b))
```

Binds `a` to `1` and `b` to `2`, but only for the body of the `let` --- the
rest of the S-expression --- and then executes the body.

This is similar in C to opening a new block:

```c
int result;
{
  int a = 1;
  int b = 2;
  result = a + b;
}
```

but it's a little different because C has a divide between *statements* and
*expressions*, whereas Lisp does not.

It's *also* different because let-expressions do not make previous binding
names available to expressions being bound. For example, the following program
should fail because it cannot find the name `a`:

```common-lisp
(let ((a 1) (b a))
  b)
```

There is a form that makes bindings available serially, but that is called
`let*` and we are not implementing that today.

For completeness' sake, there is also `let rec`, which makes names available
serially and also within the same binding. This is useful for binding recursive
or mutually recursive functions. Again, we are not implementing that today.

### Name binding implementation strategy

You'll notice two new things about let expressions:

1. They introduce ways to bind names to values, something we have to figure out
   how to keep track of
2. In order to use those names we have to figure out how to look up what the
   name means

In more technical terms, we have to add *environments* to our compiler. We can
then use those environments to map *names* to *stack locations*.

"Environment" is just a fancy word for "look-up table". In order to implement
this table, we're going to make an *association list*.

An *association list* is a list of `(key value)` pairs. Adding a pair means
tacking it on at the end (or beginning) of the list. Searching through the
table involves a linear scan, checking if keys match.

> You may be wondering why we're using this data structure to implement
> environments. Didn't I even take a data structures course in college?
> Shouldn't I know that *linear* equals *slow* and that I should *obviously*
> use a hash table?
>
> Well, hash tables have costs too. They are hard to implement right; they have
> high overhead despite being technically constant time; they incur higher
> space cost per entry.
>
> For a compiler as small as this, a tuned hash table could easily be as long
> as the rest of the compiler. Since we're also compiling small *programs*,
> we'll worry about time complexity later. It is only an implementation detail.

In order to do this, we'll first draw up an association list. We'll use a
linked list, just like cons cells:

```c
// Env

typedef struct Env {
  const char *name;
  word value;
  struct Env *prev;
} Env;
```

I've done the usual thing and overloaded `Env` to mean both "a node in the
environment" and "a whole environment". While one little `Env` struct only
holds a one name and one value, it also points to the rest of them.

This `Env` will map names (symbols) to *stack offsets*. This is because we're
going to continue our strategy of *not doing register allocation*.

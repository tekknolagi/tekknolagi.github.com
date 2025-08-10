---
title: "Compiling a Lisp: Lambda lifting"
layout: post
og_image: /assets/img/compiling-a-lisp-og-image.png
series: compiling-a-lisp
---

<span data-nosnippet>
*[first](/blog/compiling-a-lisp-0/)* -- *[previous](/blog/compiling-a-lisp-11/)*
</span>

I didn't think this day would come, but I picked up the Ghuloum tutorial again
and I got a little bit further. There's just one caveat: I have rewritten the
implementation in Python. It's available in the [same
repo](https://github.com/tekknolagi/ghuloum) in
[compiler.py](https://github.com/tekknolagi/ghuloum/blob/trunk/compiler.py).
It's brief, coming in at a little over 300 LOC + tests (compared to the C
version's 1200 LOC + tests).

I guess there's another caveat, too, which is that the Python version has no
S-expression reader. But that's fine: consider it an exercise for you, dear
reader. That's hardly the most interesting part of the tutorial.

Lifting the lambdas as required in the paper requires three things:

* Keeping track of which variables are bound
* Keeping track of which variables are free in a given lambda
* Keeping a running list of `code` objects that we create as we recurse

We have two forms that can bind variables: `let` and `lambda`. This means that
we need to recognize the names in those special expressions and modify the
environment. What environment, you ask?

Well, I have this little `LambdaConverter` class.

```python
class LambdaConverter:
    def __init__(self):
        self.labels: dict[str, list] = {}

    def convert(self, expr, bound: set[str], free: set[str]):
        match expr:
            case _:
                raise NotImplementedError(expr)
```

We keep the same `labels` dict for the entire recursive traversal of the
program, but we modify `bound` at each binding site and `free` only at lambdas.

To illustrate how they are used, let's fill in some sample expressions: `3`,
`'a`, and `#t`:

```python
class LambdaConverter:
    # ...
    def convert(self, expr, bound, free):
        match expr:
            case int(_) | Char():  # bool(_) is implied by int(_)
                return expr
```

Well, okay, sure, we don't actually need to think about variable names when we
are dealing with simple constants.

So let's look at variables:

```python
class LambdaConverter:
    # ...
    def convert(self, expr, bound, free):
        match expr:
            case str(_) if expr in bound:
                return expr
            case str(_):
                free.add(expr)
                return expr
```

We don't want to actually transform the variable uses, just add some metadata
about their uses. If we have some variable `x` bound by a `let` or a `lambda`
expression, we can leave it alone. Otherwise, we need to mark it.

```common-lisp
(let ((x 5))
  (+ x        ; bound
     y))      ; free
```

There's one irritating special case here which is that we don't want to
consider `+` (for example) as a free variable: it is a special language
primitive. So we consider `+` and the others as always bound.

```python
class LambdaConverter:
    # ...
    def convert(self, expr, bound, free):
        match expr:
            case str(_) if expr in BUILTINS:
                return expr
```

Armed with this knowledge, we can do our first recursive traversal: `if`
expressions. Since they have recursive parts and don't bind any variables, they
are the second-simplest form for this lifter.

```python
class LambdaConverter:
    # ...
    def convert(self, expr, bound, free):
        match expr:
            case ["if", test, conseq, alt]:
                return ["if",
                        self.convert(test, bound, free),
                        self.convert(conseq, bound, free),
                        self.convert(alt, bound, free)]
```

---
title: "Compiling a Lisp: Closures 1"
layout: post
date: 2020-11-25 08:00:00 PT
description: In which we compile the closure form and modify function calls
---

<span data-nosnippet>
*[first](/blog/compiling-a-lisp-0/)* -- *[previous](/blog/compiling-a-lisp-11/)*
</span>

Welcome back to the Compiling a Lisp series. Last time, we learned about
procedure calls and stack management. This time, we're going to add a new
`closure` form to enable capturing free variables and then modify the function
call operation to handle closures.

First, a definition: a *closure* is a structure that contains both code and
data. In some programming languages, these structures are called *classes*.

We are adding support for closures because that language feature is a core part
of the Lisp experience. Consider the following code snippet, which returns only
the elements of a list that are in some set:

```common-lisp
(let (
      (good-numbers (list 1 2 3))
      (some-list (list 4 5 6 1 7 8 2))
     )
  (filter (lambda (x) (list-contains good-numbers x))
          some-list)
)
  ; => (list 1 2)
```

I would argue that this is a very useful and common pattern. `filter` will only
ever pass the one argument (each element in the list through which it is
iterating) to the predicate function given. Having the predicate function know
about other data is great and means we can use `filter` instead of manually
writing some recursive function to do the same job.

There's just one problem: the lambda body explicitly references the variable
`good-numbers` even though it is not a parameter to the function! In languages
like Lisp (Python, Ruby, ...), this is allowed because `good-numbers` is
defined when the lambda is created. Since `good-numbers` is not *bound* in the
parameters list, it is a *free variable* in the lambda.

Unfortunately, the function calling we built in the previous post cannot
support this feature. Right now, every variable referenced in a `code` object
must either be bound as a parameter or part of the small set of primitive
functions shipped with the compiler. This post will add the infrastructure for
free variables and longer-lived data.

This post will *not* cover transforming `lambda` expressions to
freevar-annotated `code` objects and `closure` forms. That's the next post, so
stay tuned.

### Big ideas

In this post, we will change several compiler components:

1. Add a `closure` form that takes a bunch of expressions and allocates a
   structure on the heap to store their values
1. Add support for free variable lookup when compiling a symbol
1. Change the function call path to compile the called expression and unpack
   closure objects
1. Change `code` objects to take free variable lists in addition to parameters

### Adding the closure form

Before we emit a bunch of assembly that builds closure objects, we should be
very clear on what they look like in memory.

Closures consist of the following data lined up sequentially in memory:

* one (1) integer offset to the start of the compiled code they reference
* N objects, the freevars

Note that there is *no* length information stored on the closure itself. This
would be used for error checking at runtime, which is something we just don't
do right now.

Probably the easiest way to think about this is the following C pseudocode:

```c
Object *new_closure(int nfreevars, int code_offset, ...) {
  Object *result = malloc((nfreevars + 1) * kPointerSize);
  result[0] = new_int(code_offset);
  for (word i = 0; i < nfreevars; i++) {
    result[i] = vararg_at(i);
  }
  return result;
}

int main() {
  Object *closure = new_closure(2, 0x100, new_int(3), new_int(4));
  // do something with `closure'
}
```

The code responsible for allocating the closure makes enough space on the heap,
and then fills it in sequentially.

Our job in the compiler is a little trickier because we are not passed
`Object` pointers as arguments --- we are passed expressions, which we must
compile and store somewhere.

### Adding free variable lookup

### Unpacking closure objects

{% include compiling_a_lisp_toc.md %}

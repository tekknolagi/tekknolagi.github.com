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
holds a one name and one value, it also points to the rest of them, eventually
ending with `NULL`.

This `Env` will map names (symbols) to *stack offsets*. This is because we're
going to continue our strategy of *not doing register allocation*.

To manipulate this data structure, we will also have two functions[^1]:

```c
Env Env_bind(const char *name, word value, Env *prev);
bool Env_find(Env *env, const char *key, word *result);
```

`Env_bind` creates a new node from the given name and value, borrowing a
reference to the name, and prepends it to `prev`. Instead of returning an
`Env*`, it returns a whole struct. We'll learn more about why later, but the
"TL;DR" is that I think it requires less manual cleanup.

`Env_find` takes an `Env*` and searches through the linked list for a `name`
matching the given `key`. If it finds a match, it returns `true` and stores the
`value` in `*result`. Otherwise, it returns `false`.

We can stop at the first match because Lisp allows name *shadowing*. Shadowing
occurs when a binding at a inner scope has the same name as a binding at an
outer scope. The inner binding takes precedence:

```common-lisp
(let ((a 1))
  (let ((a 2))
    a))
; => 2
```

Alright, now we've got names and data structures. Let's implement some name
resolution and name binding.

### Compiling name resolution

Up until now, `Compile_expr` could only compile integers, characters, booleans,
`nil`, and some primitive call expressions (via `Compile_call`). Now we're
going to add a new case: symbols.

When a symbol is compiled, the compiler will look up its stack offset in the
current environment and emit a load. This opcode, `Emit_load_reg_indirect`, is
very similar to `Emit_add_reg_indirect` that we implemented for primitive
binary functions.

```c
WARN_UNUSED int Compile_expr(Buffer *buf, ASTNode *node, word stack_index,
                             Env *varenv) {
  // ...
  if (AST_is_symbol(node)) {
    const char *symbol = AST_symbol_cstr(node);
    word value;
    if (Env_find(varenv, symbol, &value)) {
      Emit_load_reg_indirect(buf, /*dst=*/kRax, /*src=*/Ind(kRbp, value));
      return 0;
    }
    return -1;
  }
  assert(0 && "unexpected node type");
}
```

If the variable is not in the environment, this is a compiler error and we
return `-1` to signal that. This is not a tremendously helpful signal. Maybe
soon we will add more helpful error messages.

Ah, yes, `varenv`. You will, like I had to, go and add an `Env*` parameter to
all relevant `Compile_XYZ` functions and then plumb it through the recursive
calls. Have fun!






<br />
<hr style="width: 100px;" />
<!-- Footnotes -->

[^1]: While I am very pleased with the `bind`/`find` symmetry, I am less
      pleased with the `Env`/`bool` asymmetry. Maybe I should have gone for
      `Node`.

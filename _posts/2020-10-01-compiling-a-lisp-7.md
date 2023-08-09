---
title: "Compiling a Lisp: Let"
layout: post
date: 2020-10-01 09:00:00 PDT
description: Compiling Lisp let expressions to x86-64
og_image: /assets/img/compiling-a-lisp-og-image.png
series: compiling-a-lisp
---

<span data-nosnippet>
*[first](/blog/compiling-a-lisp-0/)* -- *[previous](/blog/compiling-a-lisp-6/)*
</span>

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

This is similar to this very rough translated code in C:

```c
int result;
{
  int a = 1;
  int b = 2;
  result = a + b;
}
```

It's *also* different because let-expressions do not make previous binding
names available to expressions being bound. For example, the following program
should fail because it cannot find the name `a`:

```common-lisp
(let ((a 1) (b a))
  b)
```

There is a form that makes bindings available serially, but that is called
`let*` and we are not implementing that today.

For completeness' sake, there is also `letrec`, which makes names available to
all bindings, including within the same binding. This is useful for binding
recursive or mutually recursive functions. Again, we are not implementing that
today.

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
> For a compiler as small as this, a tuned hash table implementation could
> easily be as many lines of code as the rest of the compiler. Since we're also
> compiling small *programs*, we'll worry about time complexity later. It is
> only an implementation detail.

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

Let's learn about how these functions are implemented.

### Name binding implementation

`Env_bind` is a little silly looking, but it's equivalent to prepending a
node onto a chain of linked-list nodes. It returns a struct `Env` containing
the parameters passed to the function. I opted *not* to return a heap pointer
(allocated with `malloc`, etc) so that this can be easily stored in a
stack-allocated variable.

```c
Env Env_bind(const char *name, word value, Env *prev) {
  return (Env){.name = name, .value = value, .prev = prev};
}
```

*Note* that we're **pre**pending, not **ap**pending, so that names we add deeper
in a let chain shadow names from outside.

`Env_find` does a recursive linear search through the linked list nodes. It may
look familiar to you if you've already written such a function in your life.

```c
bool Env_find(Env *env, const char *key, word *result) {
  if (env == NULL)
    return false;
  if (strcmp(env->name, key) == 0) {
    *result = env->value;
    return true;
  }
  return Env_find(env->prev, key, result);
}
```

We search for the node with the string `key` and return the stack offset associated
with it.

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
int Compile_expr(Buffer *buf, ASTNode *node, word stack_index,
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

### Compiling let, finally

Now that we can resolve the names, let's go ahead and compile the expressions
that bind them.

We'll have to add a case in `Compile_expr`. We could add it in the body of
`Compile_expr` itself, but there is some helpful setup in `Compile_call`
already. It's a bit of a misnomer, since it's not a call, but oh well.

```c
int Compile_call(Buffer *buf, ASTNode *callable, ASTNode *args,
                 word stack_index, Env *varenv) {
  if (AST_is_symbol(callable)) {
    // ...
    if (AST_symbol_matches(callable, "let")) {
      return Compile_let(buf, /*bindings=*/operand1(args),
                         /*body=*/operand2(args), stack_index,
                         /*binding_env=*/varenv,
                         /*body_env=*/varenv);
    }
  }
  assert(0 && "unexpected call type");
}
```

We have two cases to handle: no bindings and some bindings. We'll tackle these
recursively, with no bindings being the base case. For that reason, I added a
helper function `Compile_let`[^2].

As with all of the other compiler functions, we pass it an machine code buffer,
a stack index, and an environment. Unlike other functions, we passed it two
expressions and two environments.

I split up the bindings and the body so we can more easily recurse on the
bindings as we go through them. When we get to the end (the base case), the
bindings will be `nil` and we can just compile the `body`.

We have two environments for the reason I mentioned above: when we're
evaluating the expressions that we're binding the names to, we can't add
bindings iteratively. We have to evaluate them in the parent environment. It'll
be come clearer in a moment how that works.

We'll tackle the simple case first --- no bindings:

```c
int Compile_let(Buffer *buf, ASTNode *bindings, ASTNode *body,
                word stack_index, Env *binding_env, Env *body_env) {
  if (AST_is_nil(bindings)) {
    // Base case: no bindings. Compile the body
    _(Compile_expr(buf, body, stack_index, body_env));
    return 0;
  }
  // ...
}
```

In that case, we compile the body using the `body_env` as the environment. This
is the environment that we will have added all of the bindings to.

In the case where we *do* have bindings, we can take the first one off and pull
it apart:

```c
  // ...
  assert(AST_is_pair(bindings));
  // Get the next binding
  ASTNode *binding = AST_pair_car(bindings);
  ASTNode *name = AST_pair_car(binding);
  assert(AST_is_symbol(name));
  ASTNode *binding_expr = AST_pair_car(AST_pair_cdr(binding));
  // ...
```

Once we have the `binding_expr`, we should compile it. The result will end up
in `rax`, per our internal compiler convention. We'll then store it in the next
available stack location:

```c
  // ...
  // Compile the binding expression
  _(Compile_expr(buf, binding_expr, stack_index, binding_env));
  Emit_store_reg_indirect(buf, /*dst=*/Ind(kRbp, stack_index),
                          /*src=*/kRax);
  // ...
```

We're compiling this binding expression in `binding_env`, the parent
environment, because we don't want the previous bindings to be visible.

Once we've generated code to store it on the stack, we should register that
stack location with the binding name in the environment:

```c
  // ...
  // Bind the name
  Env entry = Env_bind(AST_symbol_cstr(name), stack_index, body_env);
  // ...
```

Note that we're binding it in the `body_env` because we want this to be
available to the body, but not the other bindings.

Also note that since this new binding is created in a way that does not modify
`body_env` (`entry` only points to `body_env`), it will automatically be
cleaned up at the end of this invocation of `Compile_let`. This is a little
subtle in C but it's clearer in more functional languages.

At this point we've done all the work required for one binding. All that's left
to do is emit a recursive call to handle the rest -- the `cdr` of `bindings`.
We'll decrement the `stack_index` since we just used the current `stack_index`.

```c
  // ...
  _(Compile_let(buf, AST_pair_cdr(bindings), body, stack_index - kWordSize,
                /*binding_env=*/binding_env, /*body_env=*/&entry));
  return 0;
```

That's it. That's `let`, compiled, in five steps:

1. If in the base case, compile the body
1. Pick apart the binding
1. Compile the first binding expression
1. Store it in the environment
1. Recurse

Well done!

### Internal state and debugging

It's hard to write the above code without really proving to yourself that it
does something reasonable. For that, we can add some debug print statements to
our compiler that print out at what stack offsets it is storing variables.

```
sequoia% ./bin/compiling-let --repl-eval
lisp> (let () (+ 1 2))
3
lisp> (let ((a 1)) (+ a 2))
binding 'a' at [rbp-8]
3
lisp> (let ((a 1) (b 2)) (+ a b))
binding 'a' at [rbp-8]
binding 'b' at [rbp-16]
3
lisp> (let ((a 1) (b 2)) (let ((c 3)) (+ a (+ b c))))
binding 'a' at [rbp-8]
binding 'b' at [rbp-16]
binding 'c' at [rbp-24]
6
lisp>
```

This shows us that everything looks like it is working as intended! Variables
all get sequential locations on the stack.

### Compiling let\* and modifications

A thought exercise for the reader: what would it mean to compile `let*`? What
modifications would you make to the `Compile_let` function? Take a look at the
footnote[^3] if you want to double check your answer. I'm not going to
implement it in my compiler, though. Too lazy.

### Testing

As usual, we have a testing section. There are a couple checks that a
reasonable compiler should do to reject bad programs that we've left on the
table, so we won't test:

* `let` expressions that bind a name twice
* poorly formed binding lists
* poorly formed `let` bodies

I suppose we expect programmers to write well-formed programs. You're more than
welcome to add informative error messages and helpful return values, though.

Here are some tests that I added for let. One for the base case:

```c
TEST compile_let_with_no_bindings(Buffer *buf) {
  ASTNode *node = Reader_read("(let () (+ 1 2))");
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ_FMT(Object_encode_integer(3), result, "0x%lx");
  AST_heap_free(node);
  PASS();
}
```

One for `let` with one binding:

```c
TEST compile_let_with_one_binding(Buffer *buf) {
  ASTNode *node = Reader_read("(let ((a 1)) (+ a 2))");
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ_FMT(Object_encode_integer(3), result, "0x%lx");
  AST_heap_free(node);
  PASS();
}
```

and for multiple bindings:

```c
TEST compile_let_with_multiple_bindings(Buffer *buf) {
  ASTNode *node = Reader_read("(let ((a 1) (b 2)) (+ a b))");
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, 0);
  Buffer_make_executable(buf);
  uword result = Testing_execute_expr(buf);
  ASSERT_EQ_FMT(Object_encode_integer(3), result, "0x%lx");
  AST_heap_free(node);
  PASS();
}
```

Last, most interestingly, we have a test that `let` is not actually `let*` in
disguise. We check this by compiling a `let` expression with bindings that
expect to be able to refer to one another. I wrote this test afer realizing
that I had accidentally written `let*` in the first place:

```c
TEST compile_let_is_not_let_star(Buffer *buf) {
  ASTNode *node = Reader_read("(let ((a 1) (b a)) a)");
  int compile_result = Compile_function(buf, node);
  ASSERT_EQ(compile_result, -1);
  AST_heap_free(node);
  PASS();
}
```

### Next time

That's a wrap, folks. Time to `let` go. Har har har. Next time we'll add
[`if`-expressions](/blog/compiling-a-lisp-8/), so our
programs can make decisions! Have a great day. Don't forget to tell your
friends you love them.

{% include compiling_a_lisp_toc.md %}

[^1]: While I am very pleased with the `bind`/`find` symmetry, I am less
      pleased with the `Env`/`bool` asymmetry. Maybe I should have gone for
      `Node`.

[^2]: If you're a seasoned Lisper, you may be wondering why I don't rewrite
      `let` to `lambda` and use my implementation of closures to solve this
      problem.  Well, right now we don't have support for closures because I'm
      following the Ghuloum tutorial and that requires a lot of
      to-be-implemented machinery.

      Even if we did have that machinery and rewrote `let` to `lambda`, the
      compiler would generate unnecessarily slow code. Without an optimizer to
      transform the `lambda`s back into `let`s, the na&iuml;ve implementation
      would output `call` instructions. And if we had the optimizer, well, we'd
      be back where we started with our `let` implementation.

[^3]: To compile `let*`, you could do one of two things: you could remove the
      second environment parameter and compile the bindings in the same
      environment as you compile the body. Alternatively, you could get fancy
      and make an AST rewriter that rewrites `(let* ((a 1) (b a)) xyz)` to
      `(let ((a 1)) (let ((b a)) xyz))`. The nested `let` will have the same
      effect.

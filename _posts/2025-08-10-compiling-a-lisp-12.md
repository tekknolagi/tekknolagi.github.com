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

Oh, and I also dropped the instruction encoding. I'm doing text assembly now.
Womp womp.

Anyway, lifting the lambdas as required in the paper requires three things:

* Keeping track of which variables are bound
* Keeping track of which variables are free in a given lambda
* Keeping a running list of `code` objects that we create as we recurse

We have two forms that can bind variables: `let` and `lambda`. This means that
we need to recognize the names in those special expressions and modify the
environment. What environment, you ask?

## The lifter

Well, I have this little `LambdaConverter` class.

```python
class LambdaConverter:
    def __init__(self):
        self.labels: dict[str, list] = {}

    def convert(self, expr, bound: set[str], free: set[str]):
        match expr:
            case _:
                raise NotImplementedError(expr)

def lift_lambdas(expr):
    conv = LambdaConverter()
    expr = conv.convert(expr, set(), set())
    labels = [[name, code] for name, code in conv.labels.items()]
    return ["labels", labels, expr]
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
            # ...

class LambdaTests(unittest.TestCase):
    def test_int(self):
        self.assertEqual(lift_lambdas(3), ["labels", [], 3])

    def test_bool(self):
        self.assertEqual(lift_lambdas(True), ["labels", [], True])
        self.assertEqual(lift_lambdas(False), ["labels", [], False])

    def test_char(self):
        self.assertEqual(lift_lambdas(Char("a")), ["labels", [], Char("a")])
```

Well, okay, sure, we don't actually need to think about variable names when we
are dealing with simple constants.

So let's look at variables:

```python
class LambdaConverter:
    # ...
    def convert(self, expr, bound, free):
        match expr:
            # ...
            case str(_) if expr in bound:
                return expr
            case str(_):
                free.add(expr)
                return expr
            # ...

class LambdaTests(unittest.TestCase):
    # ...
    def test_freevar(self):
        self.assertEqual(lift_lambdas("x"), ["labels", [], "x"])
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
            # ...
            case str(_) if expr in BUILTINS:
                return expr
            # ...

class LambdaTests(unittest.TestCase):
    # ...
    def test_plus(self):
        self.assertEqual(lift_lambdas("+"), ["labels", [], "+"])
```

Armed with this knowledge, we can do our first recursive traversal: `if`
expressions. Since they have recursive parts and don't bind any variables, they
are the second-simplest form for this lifter.

```python
class LambdaConverter:
    # ...
    def convert(self, expr, bound, free):
        match expr:
            # ...
            case ["if", test, conseq, alt]:
                return ["if",
                        self.convert(test, bound, free),
                        self.convert(conseq, bound, free),
                        self.convert(alt, bound, free)]
            # ...

class LambdaTests(unittest.TestCase):
    # ...
    def test_if(self):
        self.assertEqual(lift_lambdas(["if", 1, 2, 3]),
                         ["labels", [], ["if", 1, 2, 3]])
```

This test doesn't tell us much yet (other than adding an empty `labels` and not
raising an exception). But it will soon.

## Lambda

Let's think about what `lambda` does. It's a bunch of features in a trench
coat:

* bind names
* allocate code
* capture outside environment

To handle the lifting, we have to reason about all three.

First, the lambda binds its parameters as new names. In fact, those are the
*only* bound variables in a lambda. Consider:

```common-lisp
(lambda () x)
```

`x` is a free variable in that lambda! We'll want to transform that lambda
into:

```common-lisp
;                  +-parameters
;                  |  +-freevars
;                  v  v
(labels ((f0 (code () (x) x)))
  (closure f0 x))
```

Even if `x` were bound by some `let` outside the lambda, it would be free in
the lambda:

```common-lisp
(let ((x 5))
  (lambda () x))
```

That means we don't thread through the `bound` parameter to the lambda body; we
don't care what names are bound *outside* the lambda.

We also want to keep track of the set of variables that are free inside the
lambda: we'll need them to create a `code` form. Therefore, we also pass in a
new set for the lambda body's `free` set.

So far, all of this environment wrangling gives us:

```python
class LambdaConverter:
    # ...
    def convert(self, expr, bound, free):
        match expr:
            # ...
            case ["lambda", params, body]:
                body_free = set()
                body = self.convert(body, set(params), body_free)
                free.update(body_free - bound)
                # ...
                return # ???
            # ...
```

There's also `free.update(body_free - bound)` in there because any variable
free in a lambda expression is also free in the current expression---well,
except for the variables that are currently bound.

Last, we'll make a `code` form and a `closure` form. The `code` gets appended
to the global list with a new label and the label gets threaded through to the
`closure`.

```python
class LambdaConverter:
    def push_label(self, params, freevars, body):
        result = f"f{len(self.labels)}"
        self.labels[result] = ["code", params, freevars, body]
        return result

    def convert(self, expr, bound, free):
        match expr:
            # ...
            case ["lambda", params, body]:
                body_free = set()
                body = self.convert(body, set(params), body_free)
                free.update(body_free - bound)
                # vvvv new below this line vvvv
                body_free = sorted(body_free)
                label = self.push_label(params, body_free, body)
                return ["closure", label, *body_free]
            # ...
```

This is finicky! I think my first couple of versions were subtly wrong for
different reasons. Tests help a lot here. For every place in the code where I
mess with `bound` or `free` in a recursive call, I tried to have a test that
would fail if I got it wrong.

```python
class LambdaTests(unittest.TestCase):
    # ...
    def test_lambda_no_params_no_freevars(self):
        self.assertEqual(lift_lambdas(["lambda", [], 3]),
                         ["labels", [
                             ["f0", ["code", [], [], 3]],
                         ], ["closure", "f0"]])

    def test_nested_lambda(self):
        self.assertEqual(lift_lambdas(["lambda", ["x"],
                                       ["lambda", ["y"],
                                        ["+", "x", "y"]]]),
                         ["labels",
                          [["f0", ["code", ["y"], ["x"], ["+", "x", "y"]]],
                           ["f1", ["code", ["x"], [], ["closure", "f0", "x"]]]],
                          ["closure", "f1"]])
    # ... and many more, especially interacting with `let`
```

Now let's talk about the other binder.

## Let

Let's think about what `let` does by examining a confusing let expression:

```common-lisp
(let ((wolf 5)
      (x wolf))
  wolf)
```

In this expression, there are two `wolf`s. One of them is bound inside the let,
but the other is free inside the let! This is because `let` evaluates all of
its bindings without access to the bindings as they are being built up (for
that, we would need `let*`).

```common-lisp
(let ((wolf 5)   ; new binding  <-------------+
      (x wolf))  ; some other variable; free! |
  wolf)          ; bound to ------------------+
```

So this must mean that:

* we need to convert all of the bindings using the original `bound` and `free`,
  then
* only for the let body, add the new bindings (and use the original `free`)

Which gives us, in code:

```python
class LambdaConverter:
    # ...
    def convert(self, expr, bound, free):
        match expr:
            # ...
            case ["let", bindings, body]:
                new_bindings = []
                for name, val_expr in bindings:
                    new_bindings.append([name, self.convert(val_expr, bound, free)])
                names = {name for name, _ in bindings}
                new_body = self.convert(body, bound | names, free)
                return ["let", new_bindings, new_body]
            # ...

class LambdaTests(unittest.TestCase):
    # ...
    def test_let(self):
        self.assertEqual(lift_lambdas(["let", [["x", 5]], "x"]),
                         ["labels", [], ["let", [["x", 5]], "x"]])

    def test_let_lambda(self):
        self.assertEqual(lift_lambdas(["let", [["x", 5]],
                                       ["lambda", ["y"],
                                        ["+", "x", "y"]]]),
                         ["labels",
                          [["f0", ["code", ["y"], ["x"], ["+", "x", "y"]]]],
                          ["let", [["x", 5]], ["closure", "f0", "x"]]])

    def test_let_inside_lambda(self):
        self.assertEqual(lift_lambdas(["lambda", ["x"],
                                       ["let", [["y", 6]],
                                        ["+", "x", "y"]]]),
                         ["labels",
                          [["f0", ["code", ["x"], [],
                                   ["let", [["y", 6]],
                                    ["+", "x", "y"]]]]],
                          ["closure", "f0"]])

    def test_paper_example(self):
        self.assertEqual(lift_lambdas(["let", [["x", 5]],
                                         ["lambda", ["y"],
                                          ["lambda", [],
                                           ["+", "x", "y"]]]]),
                         ["labels", [
                             ["f0", ["code", [],
                               ["x", "y"], ["+", "x", "y"]]],
                             ["f1", ["code", ["y"], ["x"],
                               ["closure", "f0", "x", "y"]]],
                           ],
                          ["let", [["x", 5]], ["closure", "f1", "x"]]])
    # ... and many more, especially interacting with `lambda`
```

## Function calls

Last, and somewhat boringly, we have function calls. The only thing to call out
is again handling these always-bound primitive operators like `+`, which we
don't want to have a `funcall`:

```python
class LambdaConverter:
    # ...
    def convert(self, expr, bound, free):
        match expr:
            # ...
            case [func, *args]:
                result = [] if isinstance(func, str) and func in BUILTINS else ["funcall"]
                for e in expr:
                    result.append(self.convert(e, bound, free))
                return result
            # ...

class LambdaTests(unittest.TestCase):
    # ...
    def test_call(self):
        self.assertEqual(lift_lambdas(["f", 3, 4]), ["labels", [], ["funcall", "f", 3, 4]])
```

Now that we have these new `funcall`, and `closure` forms we have to compile
them into assembly.

## Compiling `closure`

Compiling closure forms is very similar to allocating a string or a vector. In
the first cell, we want to put a pointer to the code that backs the closure
(this will be some label like `f12`). We can get a reference to that using
`lea`, since it will be a label in the assembly. Then we write it to the heap.

Then for each free variable, we go find out where it's defined. Since we know
by construction that these are all strings, we don't need to worry about having
weird recursion issues around keeping track of a moving heap pointer. Instead,
we know it's always going to be an indirect from the stack or from the current
closure. Then we write that to the heap.

Then, since a closure is an object, we need to give it a tag. So we tag it with
`lea` because I felt cute. You could also use `or` or `add`. We store the
result in `rax` because that's our compiler contract.

Last, we bump the heap pointer by the size of the closure.

```python
def compile_expr(expr, code, si, env):
    match expr:
        # ...
        case ["closure", str(lvar), *args]:
            comment("Get a pointer to the label")
            emit(f"lea rax, {lvar}")
            emit(f"mov {heap_at(0)}, rax")
            for idx, arg in enumerate(args):
                assert isinstance(arg, str)
                comment(f"Load closure cell #{idx}")
                # Just a variable lookup; guaranteed not to allocate
                compile_expr(arg, code, si, env)
                emit(f"mov {heap_at((idx+1)*WORD_SIZE)}, rax")
            comment("Tag a closure pointer")
            emit(f"lea rax, {heap_at(CLOSURE_TAG)}")
            comment("Bump the heap pointer")
            size = align(WORD_SIZE + len(args)*WORD_SIZE)
            emit(f"add {HEAP_BASE}, {size}")
        # ...
```

So `(lambda (x) x)` compiles to:

```nasm
.intel_syntax
.global scheme_entry

f0:
mov rax, [rsp-8]
ret

scheme_entry:
# Get a pointer to the label
lea rax, f0
mov [rsi+0], rax
# Tag a closure pointer
lea rax, [rsi+6]
# Bump the heap pointer
add rsi, 16
ret
```

and if we had a closure variable, for example `(let ((y 5)) (lambda () y))`:

```nasm
.intel_syntax
.global scheme_entry

f0:
mov rax, [rdi+2]
ret

scheme_entry:
# Code for y
mov rax, 20
# Store y on the stack
mov [rsp-8], rax
# Get a pointer to the label
lea rax, f0
mov [rsi+0], rax
# Load closure cell #0
mov rax, [rsp-8]
mov [rsi+8], rax
# Tag a closure pointer
lea rax, [rsi+6]
# Bump the heap pointer
add rsi, 16
ret
```

One nicety of emitting text assembly is that I can add inline comments very
easily. That's what my `comment` function is for: it just prefixes a `#`.

...wait, hold on, why are we reading from `rdi+2` for a closure variable? That
doesn't make any sense, right?

That's because while we are reading off the closure, we are reading from a
tagged pointer. Since we know the index into the closure and also the tag at
compile-time, we can fold them into one neat indirect.

```python
def compile_lexpr(lexpr, code):
    match lexpr:
        case ["code", params, freevars, body]:
            env = {}
            for idx, param in enumerate(params):
                env[param] = stack_at(-(idx+1)*WORD_SIZE)
            # vvvv New for closures vvvv
            for idx, fvar in enumerate(freevars):
                env[fvar] = indirect(CLOSURE_BASE, (idx+1)*WORD_SIZE - CLOSURE_TAG)
            # ^^^^ New for closures ^^^^
            compile_expr(body, code, si=-(len(env)+1)*WORD_SIZE, env=env)
            code.append("ret")
        case _:
            raise NotImplementedError(lexpr)
```

Now let's call some closures...!

## Compiling `funcall`

I'll start by showing the code for `labelcall` because it's a good stepping
stone toward `funcall` (nice job, Dr Ghuloum!).

The main parts are:

* save space on the stack for the return address
* compile the args onto the stack
* adjusting the stack pointer above the locals
* call
* bringing the stack pointer back

I think in my last version (the C version) I did this recursively because
looping felt challenging to do neatly in C with the data structures I had
built but since this is Python and the wild west, we're looping.

```python
def compile_expr(expr, code, si, env):
    match expr:
        # ...
        case ["labelcall", str(label), *args]:
            new_si = si - WORD_SIZE  # Save a word for the return address
            for arg in args:
                compile_expr(arg, code, new_si, env)
                emit(f"mov {stack_at(new_si)}, rax")
                new_si -= WORD_SIZE
            # Align to one word before the return address
            si_adjust = abs(si+WORD_SIZE)
            emit(f"sub rsp, {si_adjust}")
            emit(f"call {label}")
            emit(f"add rsp, {si_adjust}")
        # ...
```

A lot of this carries over exactly to `funcall`, with a couple differences:

* save space on the stack for the return address *and the closure pointer*
* compile the function expression, which can be arbitrarily complex and results
  in a closure pointer
* save the current closure pointer
* set up the new closure pointer
* call through the new closure pointer
* restore the old closure pointer

I think the stack adjustment math was by and away the most irritating thing to
get right here. Oh, and also remembering to untag the closure when trying to
call it.

```python
def compile_expr(expr, code, si, env):
    match expr:
        # ...
        case ["funcall", func, *args]:
            # Save a word for the return address and the closure pointer
            clo_si = si - WORD_SIZE
            retaddr_si = clo_si - WORD_SIZE
            new_si = retaddr_si
            # Evaluate arguments
            for arg in args:
                compile_expr(arg, code, new_si, env)
                emit(f"mov {stack_at(new_si)}, rax")
                new_si -= WORD_SIZE
            compile_expr(func, code, new_si, env)
            # Save the current closure pointer
            emit(f"mov {stack_at(clo_si)}, {CLOSURE_BASE}")
            emit(f"mov {CLOSURE_BASE}, rax")
            # Align to one word before the return address
            si_adjust = abs(si)
            emit(f"sub rsp, {si_adjust}")
            emit(f"call {indirect(CLOSURE_BASE, -CLOSURE_TAG)}")
            emit(f"add rsp, {si_adjust}")
            emit(f"mov {CLOSURE_BASE}, {stack_at(clo_si)}")
        # ...
```

So `((lambda (x) x) 3)` compiles to:

```nasm
.intel_syntax
.global scheme_entry

f0:
mov rax, [rsp-8]
ret

scheme_entry:
# Evaluate arguments
mov rax, 12
mov [rsp-24], rax
# Get a pointer to the label
lea rax, f0
mov [rsi+0], rax
# Tag a closure pointer
lea rax, [rsi+6]
# Bump the heap pointer
add rsi, 16
# Save the current closure pointer
mov [rsp-16], rdi
mov rdi, rax
# Align to one word before the return address
sub rsp, 8
call [rdi-6]
# Restore stack and closure
add rsp, 8
mov rdi, [rsp-16]
ret
```

## Wrapping up

I think that's all there is for today, folks. We got closures, free variable
analysis, and indirect function calls. That's pretty good.

Happy hacking!

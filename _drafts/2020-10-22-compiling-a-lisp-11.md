---
title: "Compiling a Lisp: Labelled procedure calls"
layout: post
date: 2020-10-18 17:00:00 PDT
description: In which we compile procedures and procedure calls to x86-64
---

<span data-nosnippet>
*[first]({% link _posts/2020-08-29-compiling-a-lisp-0.md %})* -- *[previous]({% link _posts/2020-10-18-compiling-a-lisp-10.md %})*
</span>

Welcome back to the Compiling a Lisp series. Last time, we learned about Intel
instruction encoding. This time, we're going to use that knowledge to compile
procedure calls.

The usual function expression in Lisp is a `lambda` --- an anonymous function
that can take arguments and close over variables. Procedure calls are *not*
this. They are simpler constructs that just take arguments and return values.

We're adding procedure calls first as a stepping stone to full closure support.
This will help us get some kind of internal calling convention established and
stack manipulation figured out before things get too complicated.

After this post, we will be able to support programs like the following:

```common-lisp
(labels ((add (code (x y) (+ x y)))
         (sub (code (x y) (- x y))))
    (labelcall sub 4 (labelcall add 1 2)))
; => 1
```

and even this snazzy factorial function:

```common-lisp
(labels ((factorial (code (x) 
            (if (< x 2) 1 (* x (labelcall factorial (- x 1)))))))
    (labelcall factorial 5))
```

These are fairly pedestrian snippets of code but they demonstrate some new
features we are adding, like:

* A new `labels` form that all programs will now have to look like
* A new `code` form for describing procedures and their parameters
* A new `labelcall` expression for calling procedures

Ghuloum does not explain why he does this, but I imagine that the `labels` form
was chosen over allowing multiple separate top-level bindings because it is
easier to parse and traverse.

### Big ideas

In order to compile a program, we are going to traverse every binding in the
`labels`. For each binding, we will generate code for each `code` object.

Compiling `code` objects requires making an environment for their parameters.
We'll establish a calling convention later so that our compiler knows where to
find the parameters.

Then, once we've emitted all the code for the bindings, we will compile the
body. The body may, but is not required to, contain a `labelcall` expression.

In order to compile a `labelcall` expression, we will compile all of the
arguments provided, save them in consecutive locations on the stack, and then
emit a `call` instruction.

When all of these pieces come together, the resulting machine code will look
something like this:

```
mov rsi, rdi  # prologue
jmp main
label0:
  label0_code
label1:
  label1_code
main:
  main_code
```

You can see that all of the `code` objects will be compiled in sequence,
followed by the body of the `labels` form.

Because I have not yet figured out how to start executing at somewhere other
than the beginning of the generated code, and because I don't store generated
code in any intermediate buffers, and because we don't know the sizes of any
code in advance, I do this funky thing where I emit a `jmp` to the body code.

If you, dear reader, have a better solution, please let me know.

### A calling convention

We're not going to use the System V AMD64 ABI. That calling convention requires
that parameters are passed first in certain registers, and then on the stack.
Instead, we will pass all parameters on the stack.

This makes our code simpler, but it also means that at some point later on, we
will have to add a different kind of calling convention so that we can call
foreign functions (like `printf`, or `exit`, or something). Those functions
expect their parameters in registers. We'll worry about that later.

If we borrow and adapt the excellent diagrams from the Ghuloum tutorial, this
means that right before we make a procedure call, our stack will look like
this:

```
               Low address

           |   ...            |
           +------------------+
           |   ...            |
           +------------------+
      +->  |   arg3           | rsp-56
  out |    +------------------+
  args|    |   arg2           | rsp-48
      |    +------------------+
      +->  |   arg1           | rsp-40
           +------------------+
           |                  | rsp-32
           +------------------+
      +->  |   local3         | rsp-24
      |    +------------------+
locals|    |   local2         | rsp-16
      |    +------------------+
      +->  |   local1         | rsp-8
           +------------------+
  base     |   return point   | rsp

               High address
```

You can see the return point at `[rsp]`.

Above that are whatever local variables we have declared with `let` or perhaps
are intermediate values from some computation.

Above that is a blank space reserved for the return point. The `call`
instruction will fill it in after evaluating all the arguments.

Above the return point are all the outgoing arguments. They will appear as
locals for the procedure being called.

Finally, above the arguments, is untouched free stack space.

The `call` instruction decrements `rsp` and then writes to `[rsp]`. This means
that if we just emitted a `call`, the first local would be overwritten. No
good. Worse, the way the stack would be laid out would mean that the locals
would look like arguments.

In order to solve this problem, we need to first adjust `rsp` to point to the
last local. That way the decrement will move it below the local and the return
address will go between the locals and the arguments.

After the `call` instruction, the stack will look different. Nothing will have
actually changed, except for `rsp`. This change to `rsp` means that the callee
has a different view:

```
               Low address

           |   ...            |
           +------------------+
           |   ...            |
           +------------------+
      +->  |   arg3           | rsp-24
  in  |    +------------------+
  args|    |   arg2           | rsp-16
      |    +------------------+
      +->  |   arg1           | rsp-8
           +------------------+
  base     |   return point   | rsp
           +------------------+
           |   ~~~~~~~~~~~~   |
           +------------------+
           |   ~~~~~~~~~~~~   |
           +------------------+
           |   ~~~~~~~~~~~~   |
           +------------------+
           |   ~~~~~~~~~~~~   |

               High address
```

The squiggly tildes (`~`) indicate that the values on the stack are "hidden"
from view, since they are above `[rsp]`. The called function will *not* be able
to access those values.

If the called function wants to use one of its arguments, it can pull it off
the stack from its designated location.

> One unfortunate consequence of this calling convention is that Valgrind does
> not understand it. Valgrind cannot understand that the caller has placed data
> on the stack specifically for the callee to read it, and thinks this is a
> move/jump of an uninitialized value. This means that we get some errors now
> on these labelcall tests.

Eventually, when the function returns, the `ret` instruction will pop the
return point off the stack and jump to it. This will bring us back to the
previous call frame.

That's that! I have yet to find a good tool that will let me visualize the
stack as a program is executing. GDB probably has a mode hidden away somewhere
undocumented that does exactly this. Cutter sort of does, but it's finicky in
ways I don't really understand. Maybe one day [Kartik](http://akkartik.name/)'s
x86-64 Mu fork will be able to do this.

### Building procedure calls in small pieces

In order for this set of changes to make sense, I am going to explain all of
the pieces one at a time, top-down.

First, we'll look at the new-and-improved `Compile_entry`, which has been
updated to handle the `labels` form. This will do the usual Lisp entrypoint
setup, some checks, and the aforementioned `jmp` to the body of the code.

Then, we'll actually look at compiling the `labels`. This means going through
the bindings one-by-one and compiling their `code` objects.

Then, we'll look at what it means to compile a `code` object. Hint: it's very
much like `let`.

Last, we'll tie it all together when compiling the body of the `labels` form.

### Compiling the entrypoint

Most of this code is checking. What used to just compile an expression now
validates that what we've passed in at least vaguely looks like a well-formed
`labels` form before picking it into its component parts: the `bindings` and
the `body`.

```c
int Compile_entry(Buffer *buf, ASTNode *node) {
  Buffer_write_arr(buf, kEntryPrologue, sizeof kEntryPrologue);
  assert(AST_is_pair(node) && "program must have labels");
  // Assume it's (labels ...)
  ASTNode *labels_sym = AST_pair_car(node);
  assert(AST_is_symbol(labels_sym) && "program must have labels");
  assert(AST_symbol_matches(labels_sym, "labels") &&
         "program must have labels");
  // Jump to body
  word body_pos = Emit_jmp(buf, kLabelPlaceholder);
  ASTNode *args = AST_pair_cdr(node);
  ASTNode *bindings = operand1(args);
  assert(AST_is_pair(bindings) || AST_is_nil(bindings));
  ASTNode *body = operand2(args);
  _(Compile_labels(buf, bindings, body, /*labels=*/NULL, body_pos));
  Buffer_write_arr(buf, kFunctionEpilogue, sizeof kFunctionEpilogue);
  return 0;
}
```
`Compile_entry` dispatches to `Compile_labels` for iterating over all of the
labels. `Compile_labels` is a recursive function that keeps track of all the
labels so far in its arguments, so we start it off with an empty `labels`
environment.

We also pass it the location of the `jmp` so that right before it compiles the
body, it can patch the jump target.

### Compiling labels

In `Compile_labels`, we have first a base case: if there are no labels we
should just emit the body.

```c
int Compile_labels(Buffer *buf, ASTNode *bindings, ASTNode *body,
                   Env *labels, word body_pos) {
  if (AST_is_nil(bindings)) {
    Emit_backpatch_imm32(buf, body_pos);
    // Base case: no bindings. Compile the body
    _(Compile_expr(buf, body, /*stack_index=*/-kWordSize, /*varenv=*/NULL,
                   labels));
    return 0;
  }
  // ...
}
```

The jump will be a little bit useless, since there will be no intervening code
to jump over, but that's alright. We pass in an empty `varenv`, since we are
not accumulating any locals along the way; only labels. For the same reason, we
give a `stack_index` of `-kWordSize` --- the first slot.

If we *do* have labels, on the other hand, we should deal with the first label.
This means:

* pulling out the name and the code object
* binding the name to the `code` location (the current location)
* compiling the `code`

And then from there we deal with the others recursively.

```c
int Compile_labels(Buffer *buf, ASTNode *bindings, ASTNode *body,
                   Env *labels, word body_pos) {
  // ....
  assert(AST_is_pair(bindings));
  // Get the next binding
  ASTNode *binding = AST_pair_car(bindings);
  ASTNode *name = AST_pair_car(binding);
  assert(AST_is_symbol(name));
  ASTNode *binding_code = AST_pair_car(AST_pair_cdr(binding));
  word function_location = Buffer_len(buf);
  // Bind the name to the location in the instruction stream
  Env entry = Env_bind(AST_symbol_cstr(name), function_location, labels);
  // Compile the binding function
  _(Compile_code(buf, binding_code, &entry));
  _(Compile_labels(buf, AST_pair_cdr(bindings), body, &entry, body_pos));
  return 0;
}
```

It's important to note that we are binding *before* we compile the code object
*and* we are making the code location available before it is compiled! This
means that `code` objects can reference themselves and even recursively call
themselves.

Since we then pass that binding into `labels` for the recursive call, it also
means that labels can access all labels defined before them, too.

Now let's figure out what it means to compile a code object.

#### Compiling code

I split this into two functions: one helper that pulls apart `code` objects (I
didn't want to do that in `labels` because I thought it would clutter the
meaning), and one recursive function that does the work of putting the
parameters in the environment.

So `Compile_code` just pulls apart the `(code (x y z ...) body)` into the
formal parameters and the body. Since `Compile_code_impl` will need to
recursively build up information about the `stack_index` and `varenv`, we
supply those.

```c
int Compile_code(Buffer *buf, ASTNode *code, Env *labels) {
  assert(AST_is_pair(code));
  ASTNode *code_sym = AST_pair_car(code);
  assert(AST_is_symbol(code_sym));
  assert(AST_symbol_matches(code_sym, "code"));
  ASTNode *args = AST_pair_cdr(code);
  ASTNode *formals = operand1(args);
  ASTNode *code_body = operand2(args);
  return Compile_code_impl(buf, formals, code_body, /*stack_index=*/-kWordSize,
                           /*varenv=*/NULL, labels);
}
```

I said this would be like `let`, and what I meant by that was that, like `let`
bodies, code objects have "locals" --- the formal parameters. We have to bind
the names of the parameters to successive stack locations, as per our calling
convention.

In the base case, we do not have any formals, so we compile the body:

```c
int Compile_code_impl(Buffer *buf, ASTNode *formals, ASTNode *body,
                      word stack_index, Env *varenv, Env *labels) {
  if (AST_is_nil(formals)) {
    _(Compile_expr(buf, body, stack_index, varenv, labels));
    Buffer_write_arr(buf, kFunctionEpilogue, sizeof kFunctionEpilogue);
    return 0;
  }
  // ...
}
```

We also emit this function epilogue, which right now is just `ret`. I got rid
of the `push rbp`/`mov rbp, rsp`/`pop rbp` dance because we switched to using
`rsp` only instead. I alluded to this in the previous instruction encoding
interlude post.

```c
int Compile_code_impl(Buffer *buf, ASTNode *formals, ASTNode *body,
                      word stack_index, Env *varenv, Env *labels) {
  // ...
  assert(AST_is_pair(formals));
  ASTNode *name = AST_pair_car(formals);
  assert(AST_is_symbol(name));
  Env entry = Env_bind(AST_symbol_cstr(name), stack_index, varenv);
  return Compile_code_impl(buf, AST_pair_cdr(formals), body,
                           stack_index - kWordSize, &entry, labels);
}
```

{% include compiling_a_lisp_toc.md %}

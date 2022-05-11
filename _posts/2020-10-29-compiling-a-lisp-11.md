---
title: "Compiling a Lisp: Labelled procedure calls"
layout: post
date: 2020-10-29 08:00:00 PT
description: In which we compile procedures and procedure calls to x86-64
og_image: /assets/img/compiling-a-lisp-og-image.png
series: compiling-a-lisp
---

<span data-nosnippet>
*[first](/blog/compiling-a-lisp-0/)* -- *[previous](/blog/compiling-a-lisp-10/)*
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
; => 120
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
label0:
  label0_code
label1:
  label1_code
main:
  main_code
```

You can see that all of the `code` objects will be compiled in sequence,
followed by the body of the `labels` form.

<s>
Because I have not yet figured out how to start executing at somewhere other
than the beginning of the generated code, and because I don't store generated
code in any intermediate buffers, and because we don't know the sizes of any
code in advance, I do this funky thing where I emit a `jmp` to the body code.

If you, dear reader, have a better solution, please let me know.
</s>

**Edit:** *jsmith45* gave me the encouragement I needed to work on this again.
It turns out that storing the code offset of the beginning of the `main_code`
(the `labels` body) adding that to the `buf->address` works just fine. I'll
explain more below.

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

<object class="svg" type="image/svg+xml" data="/assets/img/call-stack-before-call.svg">
<pre>
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
</pre>
</object>

> Stack illustration courtesy of [Leonard][leonard].

[leonard]: https://leonardschuetz.ch/

You can see the first return point at `[rsp]`. This is the return point placed
by the caller of the *current* function.

Above that are whatever local variables we have declared with `let` or perhaps
are intermediate values from some computation.

Above that is a blank space reserved for the second return point. This is the
return point for the *about-to-be-called* function. The `call` instruction will
fill in after evaluating all the arguments.

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

<object class="svg" type="image/svg+xml" data="/assets/img/call-stack-after-call.svg">
<pre>
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
</pre>
</object>

> Stack illustration courtesy of [Leonard][leonard].

The empty colored in spaces below the return point indicate that the values on
the stack are "hidden" from view, since they are above (higher addresses than)
`[rsp]`. The called function will *not* be able to access those values.

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
setup and some checks about the structure of the AST.

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
  assert(AST_is_pair(node) && "program must have labels");
  // Assume it's (labels ...)
  ASTNode *labels_sym = AST_pair_car(node);
  assert(AST_is_symbol(labels_sym) && "program must have labels");
  assert(AST_symbol_matches(labels_sym, "labels") &&
         "program must have labels");
  ASTNode *args = AST_pair_cdr(node);
  ASTNode *bindings = operand1(args);
  assert(AST_is_pair(bindings) || AST_is_nil(bindings));
  ASTNode *body = operand2(args);
  return Compile_labels(buf, bindings, body, /*labels=*/NULL);
}
```
`Compile_entry` dispatches to `Compile_labels` for iterating over all of the
labels. `Compile_labels` is a recursive function that keeps track of all the
labels so far in its arguments, so we start it off with an empty `labels`
environment.

### Compiling labels

In `Compile_labels`, we have first a base case: if there are no labels we
should just emit the body.

```c
int Compile_labels(Buffer *buf, ASTNode *bindings, ASTNode *body,
                   Env *labels) {
  if (AST_is_nil(bindings)) {
    buf->entrypoint = Buffer_len(buf);
    // Base case: no bindings. Compile the body
    Buffer_write_arr(buf, kEntryPrologue, sizeof kEntryPrologue);
    _(Compile_expr(buf, body, /*stack_index=*/-kWordSize, /*varenv=*/NULL,
                   labels));
    Buffer_write_arr(buf, kFunctionEpilogue, sizeof kFunctionEpilogue);
    return 0;
  }
  // ...
}
```

We also set the buffer entrypoint location to the position where we're going to
emit the body of the `labels`. We'll use this later when executing, or later in
the series when we emit ELF binaries. You'll have to add a field `word
entrypoint` to your `Buffer` struct.

We pass in an empty `varenv`, since we are not accumulating any locals along
the way; only labels. For the same reason, we give a `stack_index` of
`-kWordSize` --- the first slot.

If we *do* have labels, on the other hand, we should deal with the first label.
This means:

* pulling out the name and the code object
* binding the name to the `code` location (the current location)
* compiling the `code`

And then from there we deal with the others recursively.

```c
int Compile_labels(Buffer *buf, ASTNode *bindings, ASTNode *body,
                   Env *labels) {
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
  return Compile_labels(buf, AST_pair_cdr(bindings), body, &entry);
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

I said this would be like `let`. What I meant by that was that, like `let`
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

In the case where we have at least one formals, we bind the name to the stack
location and go on our merry way.

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

That's it! That's how you compile procedures.

### Compiling labelcalls

What use are procedures if we can't call them? Let's figure out how to compile
procedure *calls*.

Code for calling a procedure must put the arguments and return address on the
stack precisely how the called procedure expects them.

> Getting this contract right can be tricky. I spent several frustrated hours
> getting this to not crash. Then, even though it didn't crash, it returned bad
> data. It turns out that I was overwriting the return address by accident and
> returning to someplace strange instead.
>
> Making handmade diagrams that track the changes to `rsp` and the stack
> *really* helps with understanding calling convention bugs.

We'll start off by dumping yet more code into `Compile_call`. This code will
look for something of the form `(labelcall name ...)`.

Before calling into a helper function `Compile_labelcall`, we get two bits of
information ready:

* `arg_stack_index`, which is the first place on the stack where args are
  supposed to go. Since we're skipping a space for the return address, this is
  one more than the current (available) slot index.
* `rsp_adjust`, which is the amount that we're going to have to, well, adjust
  `rsp`. Without locals from `let` or incoming arguments from a procedure call,
  this will be `0`. With locals and/or arguments, this will be the total amount
  of space taken up by those.

Then we call `Compile_labelcall`.

```c
int Compile_call(Buffer *buf, ASTNode *callable, ASTNode *args,
                 word stack_index, Env *varenv, Env *labels) {
    // ...
    if (AST_symbol_matches(callable, "labelcall")) {
      ASTNode *label = operand1(args);
      assert(AST_is_symbol(label));
      ASTNode *call_args = AST_pair_cdr(args);
      // Skip a space on the stack to put the return address
      word arg_stack_index = stack_index - kWordSize;
      // We enter Compile_call with a stack_index pointing to the next
      // available spot on the stack. Add kWordSize (stack_index is negative)
      // so that it is only a multiple of the number of locals N, not N+1.
      word rsp_adjust = stack_index + kWordSize;
      return Compile_labelcall(buf, label, call_args, arg_stack_index, varenv,
                               labels, rsp_adjust);
    }
    // ...
}
```

`Compile_labelcall` is one of those fun recursive functions we write so
frequently. Its job is to compile all of the arguments and store their results
in successive stack locations.

In the base case, it has no arguments to compile. It should just adjust the
stack pointer, call the procedure, adjust the stack pointer back, and return.

```c
void Emit_rsp_adjust(Buffer *buf, word adjust) {
  if (adjust < 0) {
    Emit_sub_reg_imm32(buf, kRsp, -adjust);
  } else if (adjust > 0) {
    Emit_add_reg_imm32(buf, kRsp, adjust);
  }
}

int Compile_labelcall(Buffer *buf, ASTNode *callable, ASTNode *args,
                      word stack_index, Env *varenv, Env *labels,
                      word rsp_adjust) {
  if (AST_is_nil(args)) {
    word code_address;
    if (!Env_find(labels, AST_symbol_cstr(callable), &code_address)) {
      return -1;
    }
    // Save the locals
    Emit_rsp_adjust(buf, rsp_adjust);
    Emit_call_imm32(buf, code_address);
    // Unsave the locals
    Emit_rsp_adjust(buf, -rsp_adjust);
    return 0;
  }
  // ...
}
```

`Emit_rsp_adjust` is a convenience function that takes some stack adjustment
delta. If it's negative, it will issue a `sub` instruction. If it's positive,
an `add`. Otherwise, it'll do nothing.

In the case *with* arguments, we should compile them one at a time:

```c
int Compile_labelcall(Buffer *buf, ASTNode *callable, ASTNode *args,
                      word stack_index, Env *varenv, Env *labels,
                      word rsp_adjust) {
  // ...
  assert(AST_is_pair(args));
  ASTNode *arg = AST_pair_car(args);
  _(Compile_expr(buf, arg, stack_index, varenv, labels));
  Emit_store_reg_indirect(buf, Ind(kRsp, stack_index), kRax);
  return Compile_labelcall(buf, callable, AST_pair_cdr(args),
                           stack_index - kWordSize, varenv, labels, rsp_adjust);
}
```

There, that wasn't so bad, was it? I mean, if you manage to get it right the
first time. I certainly did not. In fact, I gave up on the first version of
this compiler many months ago because I could not get procedure calls right.
With this post, I have now made it past that particular thorny milestone!

One last thing: we'll need to update the code that converts `buf->address` into
a function pointer. We have to use the `buf->entrypoint` we set earlier.

```c
uword Testing_execute_entry(Buffer *buf, uword *heap) {
  assert(buf != NULL);
  assert(buf->address != NULL);
  assert(buf->state == kExecutable);
  // The pointer-pointer cast is allowed but the underlying
  // data-to-function-pointer back-and-forth is only guaranteed to work on
  // POSIX systems (because of eg dlsym).
  byte *start_address = buf->address + buf->entrypoint;
  JitFunction function = *(JitFunction *)(&start_address);
  return function(heap);
}
```

Let's test our implementation. Maybe these tests will help you.

### Testing

I won't include all the tests in this post, but a full battery of tests is
available in `compile-procedures.c`. Here are some of them.

First, we should check that compiling code objects works:

```c
TEST compile_code_with_two_params(Buffer *buf) {
  ASTNode *node = Reader_read("(code (x y) (+ x y))");
  int compile_result = Compile_code(buf, node, /*labels=*/NULL);
  ASSERT_EQ(compile_result, 0);
  // clang-format off
  byte expected[] = {
      // mov rax, [rsp-16]
      0x48, 0x8b, 0x44, 0x24, 0xf0,
      // mov [rsp-24], rax
      0x48, 0x89, 0x44, 0x24, 0xe8,
      // mov rax, [rsp-8]
      0x48, 0x8b, 0x44, 0x24, 0xf8,
      // add rax, [rsp-24]
      0x48, 0x03, 0x44, 0x24, 0xe8,
      // ret
      0xc3,
  };
  // clang-format on
  EXPECT_EQUALS_BYTES(buf, expected);
  AST_heap_free(node);
  PASS();
}
```

As expected, this takes the first argument in `[rsp-8]` and second in
`[rsp-16]`, storing a temporary in `[rsp-24]`. This test does not test
execution because I did not want to write the testing infrastructure for
manually setting up procedure calls.

Second, we should check that defining labels works:

```c
TEST compile_labels_with_one_label(Buffer *buf) {
  ASTNode *node = Reader_read("(labels ((const (code () 5))) 1)");
  int compile_result = Compile_entry(buf, node);
  ASSERT_EQ(compile_result, 0);
  // clang-format off
  byte expected[] = {
      // mov rax, compile(5)
      0x48, 0xc7, 0xc0, 0x14, 0x00, 0x00, 0x00,
      // ret
      0xc3,
      // mov rsi, rdi
      0x48, 0x89, 0xfe,
      // mov rax, 0x2
      0x48, 0xc7, 0xc0, 0x04, 0x00, 0x00, 0x00,
      // ret
      0xc3,
  };
  // clang-format on
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_entry(buf, /*heap=*/NULL);
  ASSERT_EQ_FMT(Object_encode_integer(1), result, "0x%lx");
  AST_heap_free(node);
  PASS();
}
```

This tests for a jump over the compiled procedure bodies (CHECK!), emitting
compiled procedure bodies (CHECK!), and emitting the body of the `labels` form
(CHECK!). This one we can execute.

Third, we should check that passing arguments to procedures works:

```c
TEST compile_labelcall_with_one_param(Buffer *buf) {
  ASTNode *node = Reader_read("(labels ((id (code (x) x))) (labelcall id 5))");
  int compile_result = Compile_entry(buf, node);
  ASSERT_EQ(compile_result, 0);
  // clang-format off
  byte expected[] = {
      // mov rax, [rsp-8]
      0x48, 0x8b, 0x44, 0x24, 0xf8,
      // ret
      0xc3,
      // mov rsi, rdi
      0x48, 0x89, 0xfe,
      // mov rax, compile(5)
      0x48, 0xc7, 0xc0, 0x14, 0x00, 0x00, 0x00,
      // mov [rsp-16], rax
      0x48, 0x89, 0x44, 0x24, 0xf0,
      // call `id`
      0xe8, 0xe6, 0xff, 0xff, 0xff,
      // ret
      0xc3,
  };
  // clang-format on
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_entry(buf, /*heap=*/NULL);
  ASSERT_EQ_FMT(Object_encode_integer(5), result, "0x%lx");
  AST_heap_free(node);
  PASS();
}
```

This tests that we put the arguments in the right stack locations (skipping a
space for the return address), emit a call to the right relative address, and
that the call returns successfully. All check!!

Fourth, we should check that we adjust the stack when we have locals:

```c
TEST compile_labelcall_with_one_param_and_locals(Buffer *buf) {
  ASTNode *node = Reader_read(
      "(labels ((id (code (x) x))) (let ((a 1)) (labelcall id 5)))");
  int compile_result = Compile_entry(buf, node);
  ASSERT_EQ(compile_result, 0);
  // clang-format off
  byte expected[] = {
      // mov rax, [rsp-8]
      0x48, 0x8b, 0x44, 0x24, 0xf8,
      // ret
      0xc3,
      // mov rsi, rdi
      0x48, 0x89, 0xfe,
      // mov rax, compile(1)
      0x48, 0xc7, 0xc0, 0x04, 0x00, 0x00, 0x00,
      // mov [rsp-8], rax
      0x48, 0x89, 0x44, 0x24, 0xf8,
      // mov rax, compile(5)
      0x48, 0xc7, 0xc0, 0x14, 0x00, 0x00, 0x00,
      // mov [rsp-24], rax
      0x48, 0x89, 0x44, 0x24, 0xe8,
      // sub rsp, 8
      0x48, 0x81, 0xec, 0x08, 0x00, 0x00, 0x00,
      // call `id`
      0xe8, 0xd3, 0xff, 0xff, 0xff,
      // add rsp, 8
      0x48, 0x81, 0xc4, 0x08, 0x00, 0x00, 0x00,
      // ret
      0xc3,
  };
  // clang-format on
  EXPECT_EQUALS_BYTES(buf, expected);
  Buffer_make_executable(buf);
  uword result = Testing_execute_entry(buf, /*heap=*/NULL);
  ASSERT_EQ_FMT(Object_encode_integer(5), result, "0x%lx");
  AST_heap_free(node);
  PASS();
}
```

This tests the presence of `sub` and `add` instructions for adjusting `rsp`. It
also tests that that did not mess up our stack frame for returning to the
caller of the Lisp entrypoint --- the test harness.

Fifth, we should check that procedures can refer to procedures defined before
them:

```c
TEST compile_multilevel_labelcall(Buffer *buf) {
  ASTNode *node =
      Reader_read("(labels ((add (code (x y) (+ x y)))"
                  "         (add2 (code (x y) (labelcall add x y))))"
                  "    (labelcall add2 1 2))");
  int compile_result = Compile_entry(buf, node);
  ASSERT_EQ(compile_result, 0);
  Buffer_make_executable(buf);
  uword result = Testing_execute_entry(buf, /*heap=*/NULL);
  ASSERT_EQ_FMT(Object_encode_integer(3), result, "0x%lx");
  AST_heap_free(node);
  PASS();
}
```

And last, but definitely not least, we should check that procedures can refer
to themselves:

```c
TEST compile_factorial_labelcall(Buffer *buf) {
  ASTNode *node = Reader_read(
      "(labels ((factorial (code (x) "
      "            (if (< x 2) 1 (* x (labelcall factorial (- x 1)))))))"
      "    (labelcall factorial 5))");
  int compile_result = Compile_entry(buf, node);
  ASSERT_EQ(compile_result, 0);
  Buffer_make_executable(buf);
  uword result = Testing_execute_entry(buf, /*heap=*/NULL);
  ASSERT_EQ_FMT(Object_encode_integer(120), result, "0x%lx");
  AST_heap_free(node);
  PASS();
}
```

Ugh, beautiful. Recursion works. Factorial works. I'm so happy.

### What's next?

The logical next step in our journey is to compile `lambda` expressions. This
has some difficulty, notably that `lambda`s can capture variables from outside
the `lambda`. This means that next time, we will implement closures.

For now, revel in your newfound procedural freedom.

{% include compiling_a_lisp_toc.md %}

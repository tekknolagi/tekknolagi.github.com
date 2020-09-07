---
title: "Compiling a Lisp: Primitive binary functions"
layout: post
date: 2020-09-06 9:00:00 PDT
---

*[first]({% link _posts/2020-08-29-compiling-a-lisp-0.md %})* -- *[previous]({% link _posts/2020-09-05-compiling-a-lisp-4.md %})*

Welcome back to the "Compiling a Lisp" series. Last time, we added some
primitive unary instructions like `add1` and `integer->char`. This time, we're
going to add some primitive *binary* functions like `+` and `>`. After this
post, we'll be able to write programs like:

```common-lisp
(< (+ 1 2) (- 4 3))
```

Now that we're building primitive functions that can take two arguments, you
might notice a problem: our strategy of just storing the result in `rax` wont't
work. If we were to na&iuml;vely write something like the following to
implement `+`, then `rax` would get overwritten:

```c
    if (AST_symbol_matches(callable, "+")) {
      _(Compile_expr(buf, operand2(args)));
      _(Compile_expr(buf, operand1(args)));
      /* Oops, we just overwrote rax ^ */
      Emit_add_something(buf, /*dst=*/kRax));
      return 0;
    }
```

We could try and work around this by adding some kind of register allocation
algorithm. Or, simpler, we could decide to allocate all intermediate values on
the stack and move on with our lives. I prefer the latter.

Let's take a look at the stack at the moment we enter the compiled program:

```
+------------------+ High addresses
|  main            |
+------------------+ |
|  ~ some data ~   | |
|  ~ some data ~   | |
+------------------+ |
|  compile_test    | |
+------------------+ |
|  ~ some data ~   | |
|  ~ some data ~   | v
+------------------+
|  Testing_exe...  | rsp
+------------------+
|                  | Low addresses
```

Where `rsp` denotes the 64 bit register for the Stack Pointer.

Refresher: the call stack grows *down*. Why? Check out this [StackOverflow
answer](https://stackoverflow.com/a/54391533/569183) that quotes an architect
on the Intel 4004 and 8080 architectures.

We have `rsp` pointing at a return address inside the function
`Testing_execute_expr`, since that's what called our Lisp entrypoint. We have
some data above `rsp` that we're not allowed to poke at, and we have this empty
space below `rsp` that is in our current *stack frame*. I say "empty" because
we haven't yet stored anything there, but because it's necessarily zero-ed out.
I don't think there are any guarantees about the values in this stack frame.

We can use our stack frame to write and read values for our current Lisp
program. With every recursive subexpression, we can allocate a little more
stack space to keep track of the values. When I say "allocate", I mean
"subtract from the stack pointer", because the stack is already a contiguous
space in memory allocated for us. For example, here is how we can write to the
stack:

```
mov [rsp-8], 0x4
```

This puts the integer `4` at displacement `-8` from `rsp`. On the stack diagram
above, it would be at the slot labeled "Low addresses". It's also possible
to read with a positive or zero displacement, but those point to previous stack
frames and the return address, respectively. So let's avoid manipulating those.

Let's walk through a real example to get more hands-on experience with this
stack storage idea. We'll use the program `(+ 1 2)`. The compiled version of
that program should:

* Move `encode(2)` to `rax`
* Move `rax` into `[rsp-8]`
* Move `encode(1)` to `rax`
* Add `[rsp-8]` to `rax`

So after compiling that, the stack will look like this:

```
+------------------+ High addresses
|  Testing_exe...  | RSP
+------------------+
|  0x8             | RSP-8 (result of compile(2))
|                  | Low addresses
```

And the result will be in `rax`.

This is all well and good, but at some point we'll need our compiled programs
to make function calls of their own. When they make calls, they will need to
modify the stack pointer in order to put the return address onto the stack. Or,
simpler, you might want to at some point generate a `push` instruction, and
that modifies `rsp`.

For that reason, x86-64 comes with another register called `rbp` and it's
designed to hold the Base Pointer. While the stack pointer is supposed to track
the "top" (low address) of the stack, the base pointer is meant to keep a
pointer around to the "bottom" (high address) of our current stack frame.

This is why in a lot of compiled code

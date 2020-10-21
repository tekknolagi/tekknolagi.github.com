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

### f

{% include compiling_a_lisp_toc.md %}

---
title: "Removing runtime checks with definite assignment analysis"
layout: post
date: 2022-04-16
description: Did you assign a value to your variable? Are you sure?
---

Python is a programming language with a primary implementation called
[CPython](https://github.com/python/cpython/). CPython is implemented as a
stack-based bytecode virtual machine[^gross]. In this post, we'll talk about
using a control-flow graph (CFG) to optimize a function's bytecode and speed up
the CPython virtual machine.

## A refresher

<!-- TODO: link to patch series -->
[^gross]: This might change soon with if Sam Gross's GIL-removal patch series
    lands. Part of the patch series includes a change from the decades-old
    stack machine to a register machine to "sweeten the deal" performance-wise.

I talked at some length about Python bytecode in my
[post](/blog/discovering-basic-blocks) about lifting a graph structure out of
the bytecode. For a refresher, let's take a look at a sample Python function:

```python
def foo():
  a = 123
  return a
```

This function gets compiled to the following bytecode:

```
0 LOAD_CONST               1 (123)
2 STORE_FAST               0 (a)

4 LOAD_FAST                0 (a)
6 RETURN_VALUE
```

If you don't want to go read [the
docs](https://docs.python.org/3.8/library/dis.html), here is a very terse
description of the opcodes that operate on local variables:

* `LOAD_FAST`, for fetching a local by index and pushing it to the stack
* `STORE_FAST`, for popping off the stack and setting a local by index
* `DELETE_FAST`, for deleting a local by index

So if you read the Python code in a straight line and ignore any compiler
optimizations you might be tempted to apply, this example makes sense! The
first line, `a = 123`, compiles to a `LOAD_CONST`/`STORE_FAST` combo, and the
second line, `return a`, compiles to a `LOAD_FAST`/`RETURN_VALUE` combo.

## Speed

It's in the name: `LOAD_FAST` must be fast, right? Let's take a look at how
`LOAD_FAST` [is implemented][loadfast] in the core interpreter loop of CPython:

[loadfast]: https://github.com/python/cpython/blob/d2b55b07d2b503dcd3b5c0e2753efa835cff8e8f/Python/ceval.c#L1777-L1785

```c
#define GETLOCAL(i)     (fastlocals[i])

// ...

TARGET(LOAD_FAST) {
  PyObject *value = GETLOCAL(oparg);
  if (value == NULL) {
      goto unbound_local_error;
  }
  Py_INCREF(value);
  PUSH(value);
  DISPATCH();
}
```

At first blush, this looks pretty good. Array lookups are constant time and
often one machine instruction. It doesn't take too long to fetch some data from
memory, either, especially when the memory is in a cache. Remember when local
variable lookups in Python used to be implemented using a hash table? This is a
huge improvement.

Anyway, part of this opcode implementation includes a check to see if the local
variable is bound---checking if `value == NULL`. Why, though? Why can't the
runtime just see that `a` is defined *right there*? Why must we do all of this
extra work?[^drama]

[^drama]: I'm being a little bit dramatic. Null checks and conditional jumps
    are *extremely fast* in the grand scheme of things. When optimizing a
    virtual machine, though, it's important to remember that it's often death
    by a thousand tiny cuts---tiny slowdowns. While implementing enormous
    features like just-in-time compilation can provide big speed boosts on
    their own, speeding up a programming language generally requires many small
    improvements that can leverage one another.

I can answer the *why*: unlike in languages like C, the following is considered
a valid Python program:

```python
def foo():
  if some_condition:
    a = 123
  return a
```

There is no notion of "block scope" in Python, so all locals are
function-local. If `some_condition` is falsy and `a` never ends up getting
defined, the reference to `a` in `return a` must raise `UnboundLocalError`.

As to the *wherefore*: I cannot say. This is just How Python Is. But I can show
you how to get rid of this extra check.

The broad plan is to determine, for every reference to a local variable, if it
is definitely set to *something* at the time of reference. We don't care what
it's set to --- nor can we generally know this --- we just want to know if we
can safely fetch it without checking for `NULL`. To do this, we will build a
Cool Graph and use some Compiler Fun on that graph.

## Cool Graphs

Unfortunately, the three most common representations of CPython code (text,
AST, and bytecode) are not both not always available and not particularly
well-suited for learning cool facts about variable definitions and uses.

Fortunately, we built an awesome control-flow graph from Python bytecode in my
last post, [Discovering Basic Blocks](/blog/discovering-basic-blocks/). If you
have not already read the post, I recommend reading it. We will reuse all of
that code.

## Compiler Fun

We will build two more things into our existing structure:

1. Predecessor and successor lists for each basic block
1. Tables that tell us what names are definitely assigned at each opcode

With both of these structures, we can traverse our control-flow graph (CFG) and
replace many `LOAD_FAST` instructions with `LOAD_FAST_UNCHECKED`.

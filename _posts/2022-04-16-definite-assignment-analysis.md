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

We'll also need a new opcode, `LOAD_FAST_UNCHECKED`, that does not have the `if
(value == NULL)` check. At the time of writing, this is purely a hypothetical
opcode. One could submit a pull request to the CPython project including a
compiler patch and new opcode implementation, but it would be a decent amount
of work for uncertain gain; I will not go so far as to benchmark Python before
and after.

Let's stop waffling and figure out what, exactly, we want to do.

## Example input code

Before we try and write any code, let's look at a bunch of example programs and
their bytecode. We should be able to identify by hand which programs can be
optimized and which cannot. If we can't identify them by hand, we probably
don't have much hope identifying them programmatically.

We'll start with the smallest Python function, which does not deal with local
variables at all. Because it does not deal with locals, there are no
`LOAD_FAST` instructions to optimize.

```python
def no_locals():
  return
# Disassembly of <code object no_locals at 0x7f5b80662ea0, file "<stdin>", line 1>:
#   2           0 LOAD_CONST               0 (None)
#               2 RETURN_VALUE
```

In this next example, we one of several of Python's kinds of variable scopes.
While at first this program would seem like it should raise
`UnboundLocalError`, it might not! Since there is no indication of writing to a
local `x` in `not_local`, the compiler treats `x` as a global variable. With
every run of the function, `LOAD_GLOBAL` looks `x` up in the global variable
dictionary. No `LOAD_FAST` to optimize.

```python
def not_local():
  return x
# Disassembly of <code object not_local at 0x7fcf00f0bf50, file "<stdin>", line 1>:
#   2           0 LOAD_GLOBAL              0 (x)
#               2 RETURN_VALUE
```

```python
def ezpz():
  x = 1
  return x  # YES
# Disassembly of <code object ezpz at 0x7fbeb41e2f50, file "<stdin>", line 1>:
#   2           0 LOAD_CONST               1 (1)
#               2 STORE_FAST               0 (x)
# 
#   3           4 LOAD_FAST                0 (x)
#               6 RETURN_VALUE
```

```python
def param(x):
  return x  # YES
# Disassembly of <code object param at 0x7f6b203e9f50, file "<stdin>", line 1>:
#   2           0 LOAD_FAST                0 (x)
#               2 RETURN_VALUE
```

```python
def diamond(x):
  if x:
    result = 1
  else:
    result = 2
  return result  # YES
# Disassembly of <code object diamond at 0x7f042c63df50, file "<stdin>", line 1>:
#   2           0 LOAD_FAST                0 (x)
#               2 POP_JUMP_IF_FALSE       10
# 
#   3           4 LOAD_CONST               1 (1)
#               6 STORE_FAST               1 (result)
#               8 JUMP_FORWARD             4 (to 14)
# 
#   5     >>   10 LOAD_CONST               2 (2)
#              12 STORE_FAST               1 (result)
# 
#   6     >>   14 LOAD_FAST                1 (result)
#              16 RETURN_VALUE
```

```python
def unfortunate_del():
  x = 1
  del x
  return x  # NO
# Disassembly of <code object unfortunate_del at 0x7fe0b6e41f50, file "<stdin>", line 1>:
#   2           0 LOAD_CONST               1 (1)
#               2 STORE_FAST               0 (x)
# 
#   3           4 DELETE_FAST              0 (x)
# 
#   4           6 LOAD_FAST                0 (x)
#               8 RETURN_VALUE
```

```python
def foo():
  if some_condition:
    a = 123
  return a  # MAYBE (NO)
# Disassembly of <code object foo at 0x7f617e309030, file "<stdin>", line 1>:
#   2           0 LOAD_GLOBAL              0 (some_condition)
#               2 POP_JUMP_IF_FALSE        8
# 
#   3           4 LOAD_CONST               1 (123)
#               6 STORE_FAST               0 (a)
# 
#   4     >>    8 LOAD_FAST                0 (a)
#              10 RETURN_VALUE
```

```python
def too_late():
  if cond:
    return x  # NO
  x = 1
# Disassembly of <code object too_late at 0x7f84b0ac0030, file "<stdin>", line 1>:
#   2           0 LOAD_GLOBAL              0 (cond)
#               2 POP_JUMP_IF_FALSE        8
# 
#   3           4 LOAD_FAST                0 (x)
#               6 RETURN_VALUE
# 
#   4     >>    8 LOAD_CONST               1 (1)
#              10 STORE_FAST               0 (x)
#              12 LOAD_CONST               0 (None)
#              14 RETURN_VALUE
```

```python
def loop():
  x = 1
  x  # YES
  while True:
    x  # MAYBE (NO)
    del x  # MAYBE (NO)
# Disassembly of <code object loop at 0x7f1ead15df50, file "<stdin>", line 1>:
#   2           0 LOAD_CONST               1 (1)
#               2 STORE_FAST               0 (x)
# 
#   3           4 LOAD_FAST                0 (x)
#               6 POP_TOP
# 
#   5     >>    8 LOAD_FAST                0 (x)
#              10 POP_TOP
# 
#   6          12 DELETE_FAST              0 (x)
#              14 JUMP_ABSOLUTE            8
#              16 LOAD_CONST               0 (None)
#              18 RETURN_VALUE
```

```python
def exc():
  try:
    {}['a']
    x = 1
  except KeyError:
    print(x)  # MAYBE (NO)
# Disassembly of <code object exc at 0x7f5e54823030, file "<stdin>", line 1>:
#   2           0 SETUP_FINALLY           16 (to 18)
# 
#   3           2 BUILD_MAP                0
#               4 LOAD_CONST               1 ('a')
#               6 BINARY_SUBSCR
#               8 POP_TOP
# 
#   4          10 LOAD_CONST               2 (1)
#              12 STORE_FAST               0 (x)
#              14 POP_BLOCK
#              16 JUMP_FORWARD            28 (to 46)
# 
#   5     >>   18 DUP_TOP
#              20 LOAD_GLOBAL              0 (KeyError)
#              22 COMPARE_OP              10 (exception match)
#              24 POP_JUMP_IF_FALSE       44
#              26 POP_TOP
#              28 POP_TOP
#              30 POP_TOP
# 
#   6          32 LOAD_GLOBAL              1 (print)
#              34 LOAD_FAST                0 (x)
#              36 CALL_FUNCTION            1
#              38 POP_TOP
#              40 POP_EXCEPT
#              42 JUMP_FORWARD             2 (to 46)
#         >>   44 END_FINALLY
#         >>   46 LOAD_CONST               0 (None)
#              48 RETURN_VALUE
```

```python
```

```python
```

```python
```

```python
```

## Cool Graphs and Compiler Fun

Unfortunately, the three most common representations of CPython code (text,
AST, and bytecode) are not particularly well-suited for learning cool facts
about variable definitions and uses.

Fortunately, we built an awesome control-flow graph from Python bytecode in my
last post, [Discovering basic blocks](/blog/discovering-basic-blocks/). If you
have not already read the post, I recommend reading it. We will reuse all of
that code.

We will build two more things into our existing structure:

1. Predecessor and successor lists for each basic block
1. Tables that tell us what names are definitely assigned at each opcode

With both of these structures, we can traverse our control-flow graph (CFG) and
replace many `LOAD_FAST` instructions with `LOAD_FAST_UNCHECKED`.

<br />
<hr style="width: 100px;" />
<!-- Footnotes -->

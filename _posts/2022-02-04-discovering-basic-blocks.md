---
title: "Discovering basic blocks"
layout: post
date: 2022-02-04
---

Code comes in lots of different forms, such as text, bytecode, and control-flow
graphs (CFGs). In this post, we will learn how to construct a CFG from
bytecode. We will use Python bytecode and Python as a programming language,
but the concepts should be applicable to other bytecode and using other
programming languages.[^idiomatic]

[^idiomatic]: I have avoided idiomatic Python where it would be tricky to
    emulate in other languages.

Let's start by taking a look at a Python function:

```python
def hello_world():
    return 5
```

This function gets compiled---by the CPython compiler---into Python bytecode.
Python provides facilities to inspect the bytecode, so let's take a look.

```console?lang=python&prompt=>>>,...
>>> import dis
>>> dis.dis(hello_world)
  2           0 LOAD_CONST               1 (5)
              2 RETURN_VALUE
>>>
```

You may see this output, or you may see something different. Python bytecode is
internal and varies version to version.

Here, `hello_world` has been compiled to two opcodes, printed nicely for us.
The first loads a constant onto the value stack and the second pops it off and
exits the function. The `dis` module provides some helpful annotations such as
line numbers, bytecode offsets, and semantic meanings of opcode arguments, but
they are not present in the instruction stream. The argument `1` to
`LOAD_CONST` refers to an index into the code object's `co_consts` tuple and
the constant value is shown as the pretty `5` on the right.

But life gets more complicated, and so does code. Sometimes we must make
decisions:

```python
def decisions(x):
    if x >= 0:
        return 1
    return -1
```

which assembles to the following bytecode:

```console?lang=python&prompt=>>>,...
>>> dis.dis(decisions)
  2           0 LOAD_FAST                0 (x)
              2 LOAD_CONST               1 (0)
              4 COMPARE_OP               5 (>=)
              6 POP_JUMP_IF_FALSE       12

  3           8 LOAD_CONST               2 (1)
             10 RETURN_VALUE

  4     >>   12 LOAD_CONST               3 (-1)
             14 RETURN_VALUE
>>>
```

There is a little more going on here than in the previous example. There is now
control flow! The `if` statements gets compiled to a `POP_JUMP_IF_FALSE`. This
pops from the stack and jumps to the specified target if it is falsey (`False`,
`None`, etc). The jump target is specified as a bytecode offset in the opcode
argument: `12`. The target is annotated lower down with a `>>`, but again, this
information is derived, not present in the bytecode.

This representation makes it a little difficult to both read and analyze. In
this small example it's not so hard to put your finger on the screen and see
that `POP_JUMP_IF_FALSE` either falls through to the next instruction or jumps
to `12`. Fine. But for bigger programs, we'll have bigger problems. So we make
a CFG.

We'll do this in two passes:

**First**, walk the bytecode linearly and record all of the basic block
entrypoints. These are indices corresponding to bytecode offsets that are the
targets of jumps both relative and absolute, forward and backward.

**Second**, walk the a sorted list of basic block start indices. Create
bytecode slices from the code between adjacent indices.

> You might notice that I am using two terms: *index* and *offset*. We will be
> using both because CPython's instructions refer to offsets, but it is much
> more convenient to refer to blocks by index. Since CPython has a fixed code
> unit size of 2 bytes (1 byte for op, 1 byte for arg), this is a convenient
> conversion. For `offset->index`, divide by 2. For `index->offset`, multiply
> by 2.
>
> It's best to use a constant like `CODEUNIT_SIZE` instead of the literal 2.
> First, it explains what you *mean* by the operation more than a magic number
> does. Second, it's very possible this size might change. When it does, you
> will be unhappy. Ask me about the bytecode expansion in Skybison.


<br />
<hr style="width: 100px;" />
<!-- Footnotes -->

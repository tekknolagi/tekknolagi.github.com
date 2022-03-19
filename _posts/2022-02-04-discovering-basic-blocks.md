---
title: "Discovering basic blocks"
layout: post
date: 2022-02-04
---

## Motivation

Code comes in lots of different forms, such as text, bytecode, and control-flow
graphs (CFGs). In this post, we will learn how to construct a CFG from
bytecode. We will use Python bytecode and Python (3.6+) as a programming
language, but the concepts should be applicable to other bytecode and using
other programming languages.[^idiomatic]

[^idiomatic]: I have avoided idiomatic Python where it would be tricky to
    emulate in other languages. Some of these functions may be zingy one-liners
    in your very idiomatic Python shop. That's fine, and feel free to use those
    instead.

Let's start by taking a look at a Python function:

```python
def hello_world():
    return 5
```

This function gets compiled---by the CPython compiler---into Python bytecode.
Python provides facilities to inspect the bytecode, so let's take a look.

```
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

which compiles to the following bytecode:

```
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
control-flow! The `if` statement gets compiled to a `POP_JUMP_IF_FALSE`. This
pops from the stack and jumps to the specified target if it is falsey (`False`,
`None`, etc). The jump target is specified as a bytecode offset in the opcode
argument: `12`. The target is annotated lower down with a `>>`, but these
annotations are derived, not present in the bytecode.

This representation makes it a little difficult to both read and analyze. In
this small example it's not so hard to put your finger on the screen and see
that `POP_JUMP_IF_FALSE` either falls through to the next instruction or jumps
to `12`. Fine. But for bigger programs, we'll have bigger problems. So we make
a CFG.

## What

A CFG is composed of *basic blocks*---chunks of bytecode with no *control-flow
instructions*[^sea-of-nodes]. A control-flow instruction is a branch (jump),
return, or exception handling. Anything that transfers control somewhere else.

[^sea-of-nodes]: Not all CFGs are constructed this way. Some, like Cliff
    Click's Sea of Nodes representation, make every instruction its own node in
    the graph.

At the end of each basic block is a control-flow instruction. So a CFG for the
above function `decisions` might look like:

```
bb0:
  LOAD_FAST 0
  LOAD_CONST 1
  COMPARE_OP 5
  POP_JUMP_IF_FALSE bb2
bb1:
  LOAD_CONST 2
  RETURN_VALUE 0
bb2:
  LOAD_CONST 3
  RETURN_VALUE 0
```

Here we have three basic blocks. The first one evaluates the condition `x >= 0`
before the `if` statement. The second two are branches of the `if` statement.

You might notice that `POP_JUMP_IF_FALSE` only names one block, `bb2` as its
argument. TODO: fallthrough

## How

In short, we'll do this in two passes:

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

### In long

Python bytecode is a compressed series of bytes that is a little frustrating to
work with. Let's start by making some convenient data types that we can use for
reasoning about the bytecode. As we said above, bytecode is represented as
pairs of bytes, `(op, arg)`, called *code units*. We can represent each of
those with a class.

```python
class BytecodeOp:
    def __init__(self, op: int, arg: int, idx: int) -> None:
        self.op = op
        self.arg = arg
        self.idx = idx
```

I made the class also hold an index into the code unit array---we'll use that
later.

Now we can read `BytecodeOp`s directly off of the bytecode array of a code
object. To get a code object, read `__code__` off of any function.

```python
from typing import List
from types import CodeType as Code
CODEUNIT_SIZE = 2

def code_to_ops(code: Code) -> List[BytecodeOp]:
    bytecode = code.co_code
    result: List[BytecodeOp] = [None] * (len(bytecode) // CODEUNIT_SIZE)
    i = 0
    idx = 0
    while i < len(bytecode):
        op = bytecode[i]
        arg = bytecode[i + 1]
        result[idx] = BytecodeOp(op, arg, idx)
        i += CODEUNIT_SIZE
        idx += 1
    return result
```

We have two counters here: one for the index and one for the offset. We read
the `BytecodeOp`s off the code object by offset `i` but address them in our
result by index `idx`.

To get a feel for what this all looks like so far, let's add a `__repr__`
function to `BytecodeOp` and print out the bytecode representation of the
function `decisions` from above.

```python
import opcode

def opname(op: int) -> str:
    return opcode.opname[op]

class BytecodeOp:
    # ...
    def __repr__(self) -> str:
        return f"{opname(self.op)} {self.arg}"
```

and try it out:

```
>>> code_to_ops(decisions.__code__)
[LOAD_FAST 0, LOAD_CONST 1, COMPARE_OP 5, POP_JUMP_IF_FALSE 12, LOAD_CONST 2, RETURN_VALUE 0, LOAD_CONST 3, RETURN_VALUE 0]
>>>
```

These opcode names and arguments are the same as the `dis` module's, so we're
doing a pretty good job so far.

### Slicing and dicing

If we are going to be making blocks out of regions of bytecode, we should have
a data structure for manipulating these regions. We could use regular Python
slicing and make a bunch of `list`s. Or we could make our own little data
structure that instead acts as a view over the original bytecode array:
`BytecodeSlice`.

```python
class BytecodeSlice:
    """A slice of bytecode from [start, end)."""

    def __init__(
        self,
        bytecode: List[BytecodeOp],
        start: Optional[int] = None,
        end: Optional[int] = None,
    ) -> None:
        self.bytecode = bytecode
        self.start: int = 0 if start is None else start
        self.end: int = len(bytecode) if end is None else end

    def __repr__(self) -> str:
        return f"<BytecodeSlice start={self.start}, end={self.end}>"
```

So far, this is equivalent to a slice, complete with optional start and end
indices. We also want to know the size, probably, so we add a `size` method:

```python
class BytecodeSlice:
    # ...
    def size(self) -> int:
        return self.end - self.start
```

Now we add a little bit of Python-specific iterator stuff. If we add an
`__iter__` method to the `BytecodeSlice` class, it becomes *iterable*. This
means we can loop over it with statements like `for x in some_slice`. We can
cheat a little bit and not invent our own iterator type[^diy-iterator] by
re-using the list iterator and creating a slice:

[^diy-iterator]: If you want to make your own iterator, go for it! You will
    probably want to make a `BytecodeSliceIterator` class with its own index
    and a `__next__` method. You can also instead use `yield` to make a
    *generator*, which will work more or less the same way from the
    programmer's perspective.

```python
class BytecodeSlice:
    # ...
    def __iter__(self) -> Iterator[BytecodeOp]:
        return iter(self.bytecode[self.start : self.end])
```

Now that we have this machinery, we can go ahead and use it to make a slice of
all the bytecode:

```
>>> bytecode = code_to_ops(decisions.__code__)
>>> bytecode
[LOAD_FAST 0, LOAD_CONST 1, COMPARE_OP 5, POP_JUMP_IF_FALSE 12
, LOAD_CONST 2, RETURN_VALUE 0, LOAD_CONST 3, RETURN_VALUE 0]
>>> bytecode_slice = BytecodeSlice(bytecode)
>>> bytecode_slice
<BytecodeSlice start=0, end=8>
>>>
```

### Finding block starts

Now we want to go over all of the opcodes and find the locations where a block
starts. A block starts when another block jumps to it, or it is the first bit
of code in a function.

For most instructions, there is no control flow---so we ignore
them[^exceptions-abound]. We'll focus on three groups:

1. branching (conditional and unconditional)
1. returning
1. raising exceptions

With branching, there

[^exceptions-abound]: You might be asking, "Max, but almost any opcode can
    raise in Python.  `LOAD_ATTR`, `BINARY_OP`, etc. What do we do about that
    control flow?"

    You're right that these opcodes have implicit control flow. We're going to
    kind of ignore that here because it's not explicitly present in the source
    code. Any future analysis like dead code elimination *will* have to take it
    into account, though.


<br />
<hr style="width: 100px;" />
<!-- Footnotes -->

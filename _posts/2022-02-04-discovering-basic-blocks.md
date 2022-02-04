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

<br />
<hr style="width: 100px;" />
<!-- Footnotes -->

---
title: "Removing runtime checks with definite assignment analysis"
layout: post
date: 2022-04-16
description: Did you assign a value to your variable? Are you sure?
---

<!-- TODO: Remove -->
<meta http-equiv="refresh" content="15" />

Python is a programming language with a primary implementation called
[CPython](https://github.com/python/cpython/). CPython is implemented as a
stack-based bytecode virtual machine[^gross]. In this post, we'll talk about
using a control-flow graph (CFG) to optimize a function's bytecode and speed up
the CPython virtual machine.

## A refresher

<!-- TODO: link to patch series -->
[^gross]: This might change soon with if Sam Gross's GIL-removal patch series
    lands. Part of the patch series includes a change from the decades-old
    stack machine architecture to a register machine to "sweeten the deal"
    performance-wise.

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
variable lookups in Python used to be implemented [using a hash
table][slowlocals]? This is a huge improvement.

[slowlocals]: https://github.com/smontanaro/python-0.9.1/blob/bceb1f141ad5227dd301e8ba10213ebbd75fa192/src/ceval.c#L1170

Anyway, part of this opcode implementation includes a check to see if the local
variable is bound---checking if `value == NULL`. Why, though? Why can't the
runtime just see that `a` is defined *right there*? Why must we do all of this
extra work?[^drama]

[^drama]: I'm being a little bit dramatic. Null checks and conditional jumps
    are *extremely fast* in the grand scheme of things. When optimizing a
    virtual machine, though, it's important to remember that it's often death
    by a thousand tiny cuts---tiny slowdowns. While implementing enormous
    features like just-in-time compilation can provide speed boosts on
    their own (removing interpreter overhead, etc), speeding up a programming
    language generally requires many small improvements that can leverage one
    another. The more performance optimization we can do before native code
    generation, the better the hypothetical JIT becomes.

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
it's set to---nor can we generally know this---we just want to know if we can
safely fetch it without checking for `NULL`. To do this, we will build a Cool
Graph and use some Compiler Fun on that graph.

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

For each example, I will annotate the local variable read with `YES`, `NO`, or
`MAYBE`---which also means `NO`, since we can only rewrite variables that are
*definitely* assigned.

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
  return x  # not a local
# Disassembly of <code object not_local at 0x7fcf00f0bf50, file "<stdin>", line 1>:
#   2           0 LOAD_GLOBAL              0 (x)
#               2 RETURN_VALUE
```

Now, finally, we have a `LOAD_FAST` to work with. In this example, there is
only one path for control-flow: straight through the function. We assign the
local `x`, then reference it when returning from the function. In this example,
`x` is *definitely assigned*, so we could rewrite the `LOAD_FAST` at offset 4
to `LOAD_FAST_UNCHECKED`.

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

In this example, `x` is still bound; parameters should be considered assigned
throughout the body of the function unless explicitly deleted. In fact, the
Skybison Python runtime [uses this fact][skybison_params] to [opportunistically
rewrite][skybison_rewrite] `LOAD_FAST` without doing a full definite assignment
analysis.

[skybison_params]: https://github.com/tekknolagi/skybison/blob/93e680f96fd7cc23a3daba22bd2ec3024552fc36/runtime/bytecode.cpp#L299

[skybison_rewrite]: https://github.com/tekknolagi/skybison/blob/93e680f96fd7cc23a3daba22bd2ec3024552fc36/runtime/bytecode.cpp#L194

```python
def param(x):
  return x  # YES
# Disassembly of <code object param at 0x7f6b203e9f50, file "<stdin>", line 1>:
#   2           0 LOAD_FAST                0 (x)
#               2 RETURN_VALUE
```

Now we add some control flow to the picture. We have a branch on some
condition that we cannot eliminate at compile time. However, we don't need to
worry about the result of the condition; in both branches of the `if`, `x` is
assigned. This means that in the last block of the function---which contains
`return`---`x` is definitely assigned.

And, incidentally, `some_condition` is definitely defined.

```python
def diamond(some_condition):
  if some_condition:  # YES
    x = 1
  else:
    x = 2
  return x  # YES
# Disassembly of <code object diamond at 0x7f042c63df50, file "<stdin>", line 1>:
#   2           0 LOAD_FAST                0 (some_condition)
#               2 POP_JUMP_IF_FALSE       10
# 
#   3           4 LOAD_CONST               1 (1)
#               6 STORE_FAST               1 (x)
#               8 JUMP_FORWARD             4 (to 14)
# 
#   5     >>   10 LOAD_CONST               2 (2)
#              12 STORE_FAST               1 (x)
# 
#   6     >>   14 LOAD_FAST                1 (x)
#              16 RETURN_VALUE
```

At last, we come to the first `LOAD_FAST` that is not definitely assigned. In
fact, the read from `x` is definitely *not* assigned; the variable has been
undefined by an intervening `del`. This would be the same situation if `x` were
a parameter, too.

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

Now, we have a conditional that only defines `x` in one arm. Since we cannot
know ahead of time what `some_condition` evaluates to, we cannot mark `x` as
definitely defined. We can, however, know that `some_condition`, at least, is
definitely defined.

```python
def maybe_defined(some_condition):
  if some_condition:  # YES
    x = 123
  return x  # MAYBE (NO)
# Disassembly of <code object foo at 0x7f617e309030, file "<stdin>", line 1>:
#   2           0 LOAD_FAST                0 (some_condition)
#               2 POP_JUMP_IF_FALSE        8
# 
#   3           4 LOAD_CONST               1 (123)
#               6 STORE_FAST               1 (x)
# 
#   4     >>    8 LOAD_FAST                1 (x)
#              10 RETURN_VALUE
```

I don't really know why you would write code that uses a local variable before
its first assignment, but here is some sample code that does. Despite `x` being
assigned below, it is not in scope for its first use -- in the `return`.

```python
def too_late(some_condition):
  if some_condition:  # YES
    return x  # NO
  x = 1
  return x  # YES
# Disassembly of <code object too_late at 0x7ff353d22f50, file "<stdin>", line 1>:
#   2           0 LOAD_FAST                0 (some_condition)
#               2 POP_JUMP_IF_FALSE        8
# 
#   3           4 LOAD_FAST                1 (x)
#               6 RETURN_VALUE
# 
#   4     >>    8 LOAD_CONST               1 (1)
#              10 STORE_FAST               1 (x)
# 
#   5          12 LOAD_FAST                1 (x)
#              14 RETURN_VALUE
```

Now we have our first backward branch in the CFG! What should happen in a loop?
In this example loop, the first time we go around, `x` will be defined for its
load and also its `del`. The next times around, it will be undefined. So we
cannot optimize the code in the loop---just the code beforehand.

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

Last, we have some exception handling. Exception handling is like other control
flow, except that I can never remember in what order or circumstances `try`,
`except`, `else`, and `finally` blocks are executed. So let's look them up...

From the [docs](https://docs.python.org/3.8/tutorial/errors.html):

> The try statement works as follows.
> * First, the `try` clause (the statement(s) between the `try` and `except`
>   keywords) is executed.
> * If no exception occurs, the `except` clause is skipped and execution of the
>   `try` statement is finished.
> * If an exception occurs during execution of the `try` clause, the rest of
>   the clause is skipped. Then, if its type matches the exception named after
>   the `except` keyword, the `except` clause is executed, and then execution
>   continues after the `try`/`except` block.
> * If an exception occurs which does not match the exception named in the
>   `except` clause, it is passed on to outer `try` statements; if no handler
>   is found, it is an unhandled exception and execution stops with a message
>   as shown above.
> ...
> The `try ... except` statement has an optional `else` clause, which, when
> present, must follow all `except` clauses. It is useful for code that must be
> executed if the `try` clause does not raise an exception.
> ...
> If a `finally` clause is present, the `finally` clause will execute as the
> last task before the `try` statement completes.

...and then there are many more bullet points about how `finally` clauses work
and in what order they execute and what happens with
`return`/`break`/`continue` inside a `finally`, and so on and so forth. For
this reason, I will not be covering exception handling with `finally` clauses
in this article. It's extremely Python-specific, confusing, and detracts from
the main purpose.

In any case, here is a small `try`/`except`/`else` example:

```python
def exc():
  try:
    {}['a']
    x = 1
    print(x)  # YES
  except KeyError:
    print(x)  # MAYBE (NO)
  else:
    print(x)  # MAYBE (NO)
# Disassembly of <code object exc at 0x7f1ead15df50, file "<stdin>", line 1>:
#   2           0 SETUP_EXCEPT            24 (to 26)
# 
#   3           2 BUILD_MAP                0
#               4 LOAD_CONST               1 ('a')
#               6 BINARY_SUBSCR
#               8 POP_TOP
# 
#   4          10 LOAD_CONST               2 (1)
#              12 STORE_FAST               0 (x)
# 
#   5          14 LOAD_GLOBAL              0 (print)
#              16 LOAD_FAST                0 (x)
#              18 CALL_FUNCTION            1
#              20 POP_TOP
#              22 POP_BLOCK
#              24 JUMP_FORWARD            28 (to 54)
# 
#   6     >>   26 DUP_TOP
#              28 LOAD_GLOBAL              1 (KeyError)
#              30 COMPARE_OP              10 (exception match)
#              32 POP_JUMP_IF_FALSE       52
#              34 POP_TOP
#              36 POP_TOP
#              38 POP_TOP
# 
#   7          40 LOAD_GLOBAL              0 (print)
#              42 LOAD_FAST                0 (x)
#              44 CALL_FUNCTION            1
#              46 POP_TOP
#              48 POP_EXCEPT
#              50 JUMP_FORWARD            10 (to 62)
#         >>   52 END_FINALLY
# 
#   9     >>   54 LOAD_GLOBAL              0 (print)
#              56 LOAD_FAST                0 (x)
#              58 CALL_FUNCTION            1
#              60 POP_TOP
#         >>   62 LOAD_CONST               0 (None)
#              64 RETURN_VALUE
```

Even without `finally`, there's a *lot* of control flow. See all the `>>`
arrows? So many jump targets. I am not sure why there is an `END_FINALLY`
opcode in there, given that we did not have a `finally` clause. A quick look at
our work team's [Python compiler in Python][pycompiler] shows that it's emitted
in all `try`/`except` code generation. Weird.

[pycompiler]: https://github.com/facebookarchive/python-compiler/blob/5a9a30b3d5fae5337ff449030873a58b35e875a4/compiler/pycodegen.py#L997

## Cool Graphs and Compiler Fun

You have now read a lot of Python code and bytecode and have an idea of what
cases we can optimize. Unfortunately, the three most common representations of
CPython code (text, abstract syntax tree (AST), and bytecode) are not
particularly well-suited for learning cool facts about variable definitions and
uses. The text would require parsing into an AST, the AST has a lot of node
types and still not much helpful control-flow
information[^tree-abstract-interpretation], and the linear nature of the
bytecode obscures branches.

[^tree-abstract-interpretation]: You can probably write a similar abstract
    interpretation pass for the AST as we did for bytecode, but I have not seen
    people do this often. I imagine the Graal/Truffle project does this,
    though, as their whole thing is compiling straight off the AST.

Fortunately, we built an awesome control-flow graph from Python bytecode in my
last post, [Discovering basic blocks](/blog/discovering-basic-blocks/). If you
have not already read the post, I recommend reading it. We will reuse all of
that code.

We will build two more things into our existing structure:

1. Predecessor and successor lists for each basic block
1. Tables that tell us what names are definitely assigned at each opcode

With both of these structures, we can traverse our control-flow graph (CFG) and
replace many `LOAD_FAST` instructions with `LOAD_FAST_UNCHECKED`.

## Modifying our existing code

The [Addendum](/blog/discovering-basic-blocks#addendum)from the previous post
has some code that makes successor sets, and we're going to add on to it. I'll
rehash all of it here for clarity.

### Step 1: Add predecessor/successors to blocks

We have a set of basic blocks, each of which has some bytecode (a
`BytecodeSlice`) and an identifier. Unfortunately, not every block's bytecode
ends with a *terminator* instruction---sometimes adjacent blocks end with any
random instruction and "fall through" to the next one. For an example, see the
bytecode for `diamond` above. We can make the control flow a little bit more
explicit, even without modifying the bytecode, by adding two sets to each
block: `preds` and `succs`.

For each block, get the successors and link them up.

```python
def compute_preds_succs(block_map: BlockMap):
    for block in block_map.idx_to_block.values():
        for idx in block.bytecode.successor_indices():
            succ = block_map.idx_to_block[idx]
            block.succs.add(succ)
            succ.preds.add(block)
```

This method `successor_indices` on `BytecodeSlice` looks at the terminator and
returns either a zero-element, one-element, or two-element tuple of successors
to the current block.

```python
class BytecodeSlice:
    # ...
    def successor_indices(self) -> Tuple[int]:
        last = self.last()
        if last.is_branch():
            if last.is_unconditional_branch():
                return (last.jump_target_idx(),)
            return (last.next_instr_idx(), last.jump_target_idx())
        if instr.is_return() or instr.is_raise():
            # Raise and return do not have any successors
            return ()
        return (last.next_instr_idx(),)
```

## Extension: Adding it to CPython

<br />
<hr style="width: 100px;" />
<!-- Footnotes -->

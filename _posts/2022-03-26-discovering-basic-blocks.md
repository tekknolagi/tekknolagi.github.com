---
title: "Discovering basic blocks"
layout: post
date: 2022-03-26
description: Lifting a graph structure out of Python bytecode
series: runtime-opt
---

## Motivation

Code comes in lots of different forms, such as text, bytecode, and other data
structures common in compilers---like control-flow graphs (CFGs). CFGs are
commonly used in compiler internals for analysis and optimization, and in
reverse engineering for lifting structure out of linear assembly code.

In this post, we will learn how to construct a CFG from a subset of CPython
(3.6+) bytecode. We will also coincidentally be using Python (3.6+) as a
programming language, but the concepts should be applicable to other bytecode
and using other programming languages.[^idiomatic]

[^idiomatic]: I have avoided idiomatic Python where it would be tricky to
    emulate in other languages. Some of these functions may be zingy one-liners
    in your very idiomatic Python shop. That's fine, and feel free to use those
    instead.

## Python internals

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
pops from the stack and jumps to the specified target if it is falsy (`False`,
`None`, empty list, etc). The jump target is specified as a bytecode offset in
the opcode argument: `12`. The target is annotated lower down with a `>>`, but
these annotations are derived, not present in the bytecode.

This representation makes it a little difficult to both read and analyze. In
this small example it's not so hard to put your finger on the screen and see
that `POP_JUMP_IF_FALSE` either falls through to the next instruction or jumps
to `12`. Fine. But for bigger programs, we'll have bigger problems. So we make
a CFG.

## What's in a CFG

A CFG is composed of *basic blocks*---chunks of bytecode with no *control-flow
instructions*[^sea-of-nodes] inside those chunks. A control-flow instruction is
a branch (jump), return, or exception handling. Anything that transfers
control[^control] somewhere else.

[^sea-of-nodes]: Not all CFGs are constructed this way. Some, like Cliff
    Click's Sea of Nodes representation, make every instruction its own node in
    the graph.

[^control]: Ever get an error when compiling your C or C++ program like
    "control reaches end of non-void function"? That's the same kind of
    "control"! Control refers to the notion path your program takes as it is
    executing. That particular error or warning message refers to the program
    analyzer noticing that there might be a path through your program that
    falls off the end without returning a value---even though you promised the
    function would return a value.

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
argument. If the top of the stack is false, control will flow to `bb2`. If it
is not, control will implicitly fall-through to the next block, `bb1`. You can
choose to make this explicit in your printing so it says `POP_JUMP_IF_FALSE
bb1, bb2` if you like[^new-ir].

[^new-ir]: Alternatively, you can turn bytecode-based blocks into a different
    IR that does not rely on blocks being in a particular order. This is best
    saved for another post.

Let's build ourselves a CFG.

## How to build a CFG

### In short

We'll do this in two passes:

**First**, walk the bytecode linearly and record all of the basic block
entrypoints. These are indices corresponding to bytecode offsets that are the
targets of jumps both relative and absolute, forward and backward.

**Second**, walk the a sorted list of basic block start indices. Create
bytecode slices and blocks from the code between adjacent indices.

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
> will be unhappy. Ask me about the bytecode expansion in
> [Skybison](https://github.com/tekknolagi/skybison).

After that, you will have your CFG.

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
    num_instrs = len(bytecode) // CODEUNIT_SIZE
    result: List[BytecodeOp] = [None] * num_instrs
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
from typing import Iterator

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

Let's make some slices.

### Finding block starts

We want to go over all of the opcodes and find the locations where a block
starts. A block starts when another block jumps to it and ends with a
control-flow instruction.

For most instructions, there is no control-flow---so we ignore
them[^exceptions-abound]. Blocks will be composed of mostly "normal"
instructions, but they are uninteresting right now. While creating our CFG,
we'll instead focus on three groups of control-flow instructions:

[^exceptions-abound]: You might be asking, "Max, but almost any opcode can
    raise in Python. `LOAD_ATTR`, `BINARY_OP`, etc. What do we do about that
    control-flow?"

    You're right that these opcodes have implicit control-flow. We're going to
    kind of ignore that here because it's not explicitly present in the source
    code. Any future analysis like dead code elimination *will* have to take it
    into account, though.

1. branching (conditional and unconditional)
1. returning
1. raising exceptions[^catching-exceptions]

[^catching-exceptions]: I haven't mentioned catching exceptions here because
    it's a bit trickier to handle and I am not confident about the
    implementation. Maybe I will add it later.

Each of these instructions terminates a block. Branches have two next blocks:
the jump target and the implicit fall-through into the next block. Returns
*sometimes* have an implicit fall-through block; if a return is the last
instruction in a code object, there is no next block. Last, raises only ever
have an implicit fall-through block.

> The CPython compiler makes a nice guarantee that all Python code objects will
> end with a `return`. If there is no `return` in the source code, the compiler
> will add an invisible `return None`. This means there is no way to "fall off
> the edge" of the bytecode. This makes our lives a little easier.

Let's iterate over our instruction slice and build up our basic blocks. We'll
define a function `create_blocks` that takes in a slice and returns a map of
bytecode indices to basic blocks.

To make this code readable, we'll add some helper methods to `BytecodeOp`.
We'll also use Python's `opcode` module to be able to work with opcodes
symbolically.


```python
Op = type("Op", (object,), opcode.opmap)
```

The above is a bit of sneaky metaprogramming that uses the existing dictionary
of opcode names to opcode numbers (`opcode.opmap`) as a template for building a
new type, `Op`. Then we can use all the keys in the dictionary as if they were
attributes on the type. I know I said I wouldn't do any zingy one-liners, but
this saves us a lot of re-typing of information that is already inside CPython.
Anyway, onto the helper methods on `BytecodeOp`.

First, we need to know if an instruction is a branch:

```python
class BytecodeOp:
    # ...
    def is_branch(self) -> bool:
        return self.op in {
            Op.FOR_ITER,
            Op.JUMP_ABSOLUTE,
            Op.JUMP_FORWARD,
            Op.JUMP_IF_FALSE_OR_POP,
            Op.JUMP_IF_TRUE_OR_POP,
            Op.POP_JUMP_IF_FALSE,
            Op.POP_JUMP_IF_TRUE,
        }

    def is_relative_branch(self) -> bool:
        return self.op in {Op.FOR_ITER, Op.JUMP_FORWARD}
```

The opcode `FOR_ITER` is used at the beginning of a loop and is responsible for
calling `__next__` on a Python iterator---and jumping to the end of the loop
if that raises `StopIteration`. The others all have "JUMP" in the name.

We also need to know if it's a *relative* branch. For capital-r Reasons, some
opcodes refer to their jump targets absolutely---directly by the target's
bytecode offset. Others refer to their jump targets relatively---as a delta
from the branch instruction. So, to calculate the jump target, we need this
information.

Let's calculate some jump targets. For relative branches, we need the offset of
the next instruction so that we can add the delta in the oparg.

Since every `BytecodeOp` knows its own index, it also knows the next index (the
fall-through): add `1`. Since all CPython opcodes are two bytes long, we can
convert that into an offset: multiply by `CODEUNIT_SIZE`.

```python
class BytecodeOp:
    # ...
    def next_instr_idx(self) -> int:
        return self.idx + 1

    def next_instr_offset(self) -> int:
        return self.next_instr_idx() * CODEUNIT_SIZE
```

Then, if the instruction is a relative branch, we add the delta. Otherwise, we
return the absolute offset in the oparg.

Since outside of `BytecodeOp`, we will refer to instructions exclusively by
index, we also provide a function to get the jump target's index.

```python
class BytecodeOp:
    # ...
    def jump_target(self) -> int:
        if self.is_relative_branch():
            return self.next_instr_offset() + self.arg
        return self.arg

    def jump_target_idx(self) -> int:
        return self.jump_target() // CODEUNIT_SIZE
```

If your implementation language supports it[^python-privates], feel free to
make `jump_target` a private method.

[^python-privates]: Python "supports" private identifiers. If in a class `C` you
    start an idenfier with two underscores like `__foo` (but do not end it with
    two underscores... and maybe some other rules... see uses of `_Py_Mangle`),
    it will get silently rewritten `_C__foo`. This is about all Python provides
    as far as private identifiers go.

For return instructions and raise instructions, our predicates are fairly
straightforward:

```python
class BytecodeOp:
    # ...
    def is_return(self) -> bool:
        return self.op == Op.RETURN_VALUE

    def is_raise(self) -> bool:
        return self.op == Op.RAISE_VARARGS
```

Now we can walk the instructions in our bytecode and find all of the places a
basic block starts.

We first mark the instruction at index 0 as starting a block, since it is the
entrypoint to the function---and, except for loops, no code would otherwise
cause it to be marked as a block start.

Then we find all of the control instructions.

```python
def create_blocks(instrs: BytecodeSlice) -> BlockMap:
    block_starts = set([0])
    num_instrs = instrs.size()
    for instr in instrs:
        if instr.is_branch():
            block_starts.add(instr.next_instr_idx())
            block_starts.add(instr.jump_target_idx())
        elif instr.is_return():
            next_instr_idx = instr.next_instr_idx()
            if next_instr_idx < num_instrs:
                block_starts.add(next_instr_idx)
        elif instr.is_raise():
            block_starts.add(instr.next_instr_idx())
```

Once we have the start indices, we also have the *end* indices! If we have, for
example, starts at 0, 4, and 8, we know that the first block goes 0 through 3.
The second block goes 4 through 7. And the last block goes from 8 through to
the end of the bytecode. With this information, we can make bytecode slices and
blocks.

The `Block` class is not very interesting. Its job is to hold a unique
identifier and a `BytecodeSlice`.

```python
class Block:
    def __init__(self, id: int, bytecode: BytecodeSlice):
        self.id: int = id
        self.bytecode: BytecodeSlice = bytecode
```

The block map is not so interesting either. Its primary job is to wrap a dict
of block indices to blocks and make a pretty string representation of the CFG.

```python
class BlockMap:
    def __init__(self) -> None:
        self.idx_to_block: Dict[int, Block] = {}

    def add_block(self, idx, block):
        self.idx_to_block[idx] = block

    def __repr__(self) -> str:
        result = []
        for block in self.idx_to_block.values():
            result.append(f"bb{block.id}:")
            for instr in block.bytecode:
                if instr.is_branch():
                    target_idx = instr.jump_target_idx()
                    target = self.idx_to_block[target_idx]
                    result.append(f"  {opname(instr.op)} bb{target.id}")
                else:
                    result.append(f"  {instr}")
        return "\n".join(result)
```

Though we could use the block indices as identifiers, since they are unique to
each block, we will instead allocate new numbers counting up from zero. This
makes the CFG a little easier to read when printed, if nothing else.

Let's return to `create_blocks`. At this point we have a set of bytecode
indices that correspond to places blocks should start: `block_starts`. We want
to iterate through them in order, from 0 to N, build slices from the (start,
end) pairs, and build blocks from the slices[^pithy].

[^pithy]: If you are looking for a pithy one-liner, I think you can use slicing
    and `itertools.zip_longest`.

If we're building a block and there is no end index, that means we're building
the last block and should use the number of instructions as the end index.

```python
def create_blocks(instrs: BytecodeSlice) -> BlockMap:
    # ...
    num_blocks = len(block_starts)
    block_starts_ordered = sorted(block_starts)
    block_map = BlockMap()
    for i, start_idx in enumerate(block_starts_ordered):
        end_idx = block_starts_ordered[i + 1] if i + 1 < num_blocks else num_instrs
        block_instrs = BytecodeSlice(instrs.bytecode, start_idx, end_idx)
        block_map.add_block(start_idx, Block(i, block_instrs))
    return block_map
```

Let's check our work:

```
>>> bytecode = code_to_ops(decisions.__code__)
>>> bytecode
[LOAD_FAST 0, LOAD_CONST 1, COMPARE_OP 5, POP_JUMP_IF_FALSE 12
, LOAD_CONST 2, RETURN_VALUE 0, LOAD_CONST 3, RETURN_VALUE 0]
>>> bytecode_slice = BytecodeSlice(bytecode)
>>> bytecode_slice
<BytecodeSlice start=0, end=8>
>>> block_map = create_blocks(bytecode_slice)
>>> block_map
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
>>>
```

Which looks like what we expected. Awesome.

## Conclusion

Now that you have your CFG in hand, you may want to analyze it. You may want to
do dead code elimination, or flow typing, or even compile it to a lower-level
representation. You can do this straight off the bytecode blocks, but it will
be easier to do so on an infinite register based IR. Perhaps in [static single
assignment (SSA)][ssa] form. If I have time, I will write about those soon. If I
don't, well, you have the rest of the internet to consult.

[ssa]: https://pp.info.uni-karlsruhe.de/uploads/publikationen/braun13cc.pdf

## Addendum

Reddit user moon-chilled has pointed out that this graph does not convert
fallthroughs into explicit jumps. For example, the following Python code:

```python
def f(x):
    if x:
        y = 1
    else:
        y = 2
    return y
```

gets converted into the following CFG:

```
bb0:
  LOAD_FAST 0
  POP_JUMP_IF_FALSE bb2
bb1:
  LOAD_CONST 1
  STORE_FAST 1
  JUMP_FORWARD bb3
bb2:
  LOAD_CONST 2
  STORE_FAST 1
bb3:
  LOAD_FAST 1
  RETURN_VALUE 0
```

The block `bb2` "should" have a jump to `bb3`, but instead it ends abruptly
with a non-control instruction. In this post, I focused on lifting structure
out of the existing code and not modifying it (adjusting all the indices is
annoying)---but it would be better to add that as an explicit jump.

Consider, for example, a compiler pass that re-ordered blocks. The fallthrough
would break if `bb3` did not always follow `bb2`. Better to have an explicit
jump to it.

It's not so hard to pull this implicit fallthrough into an explicit jump. We
can gather all of the predecessors and successors for each block using our
existing infrastructure. To start, we add a predicate on bytecode ops to check
if they are unconditional jumps:

```python
class BytecodeOp:
    # ...
    def is_unconditional_branch(self) -> bool:
        return self.op in {Op.JUMP_ABSOLUTE, Op.JUMP_FORWARD}
```

This is useful in finding the successors of a given opcode. Conditional jumps
will have two (one for the true case and one for the false case) whereas
unconditional jumps will have only one. And the rest is as before:

```python
class BytecodeSlice:
    # ...
    def last(self) -> BytecodeOp:
        return self.bytecode[self.end - 1]

    def successor_indices(self) -> Tuple[int]:
        last = self.last()
        if last.is_branch():
            if last.is_unconditional_branch():
                return (last.jump_target_idx(),)
            return (last.next_instr_idx(), last.jump_target_idx())
        if last.is_return() or last.is_raise():
          # Raise and return do not have any successors
          return ()
        # Other instructions have implicit fallthrough to the next block
        return (last.next_instr_idx(),)
```

Then, finally, we can iterate over all of the blocks and link up who jumps to
what.

```python
class Block:
    # ...
    def __init__(self, id: int, bytecode: BytecodeSlice):
        # ...
        self.succs: Set[Block] = set()

def compute_successors(block_map: BlockMap):
    for block in block_map.idx_to_block.values():
        block.succs = tuple(
            block_map.idx_to_block[idx]
            for idx in bytecode_slice.successor_indices()
        )
```

A good next step would be to turn the bytecode instructions into your own
internal representation and leave the `(op, arg)` pair limitation behind.

### See also

Building CFGs [inside PyPy](https://rpython.readthedocs.io/en/latest/translation.html#building-flow-graphs).

Tailbiter, a [Python bytecode compiler written in
Python](https://codewords.recurse.com/issues/seven/dragon-taming-with-tailbiter-a-bytecode-compiler),
and a [newer version](https://github.com/facebookarchive/python-compiler)
I helped work on.

A [Python interpreter written in Python](https://www.aosabook.org/en/500L/a-python-interpreter-written-in-python.html).

---
title: "Building SSA from stack-based bytecode"
layout: post
date: 2023-07-01
series: runtime-opt
---

CPython, among other runtimes (CRuby, OpenJDK, etc), uses a stack-based
bytecode representation to execute programs. In the [last
post](/blog/discovering-basic-blocks/), we built a control-flow graph from the
bytecode. In this post, we will convert the stack-based bytecode to static
single-assignment (SSA) register-based bytecode. We will do so using magic
called *abstract interpretation*. I'm not the inventor of any of these
techniques, and will cite relevant research both in the post and at the end.

## Motivation

On the scale of "purely interpreted" to "purely compiled"[^spectrum],
performance usually comes with more compilation. This is because compilation
usually implies optimization, and optimization usually requires some amount of
static analysis.

[^spectrum]: I have Thoughts and Feelings about this and will probably write a
    whole other post someday.

Doing static analysis on stack-based bytecode is hard. Most static analysis
papers assume you already have your intermediate representation (IR) in SSA
form because SSA is super helpful.

Consider: what if every variable was only ever defined exactly once? Or, said
slightly differently, what if you could attach names to every expression?
Science fiction authors and computer scientists agree: names have
power[^names-power].

[^names-power]: See Douglas Engelbart's 1990 paper
    *Knowledge-Domain Interoperability and an Open Hyperdocument System* and
    Ursula K Le Guin's 1968 novel *A Wizard of Earthsea*.

While we won't be reading any hardcore optimization papers today, we will take
one medium-sized step in that direction by converting stack-based bytecode to
SSA.

## Starting point and end goal

CPython already has a bytecode compiler that turns Python functions like this:

```python
def decisions(x):
    if x:
      y = 1
    else:
      y = 2
    return y
```

into stack-based bytecode like this:

```
>>> dis.dis(decisions)
  2           0 LOAD_FAST                0 (x)
              2 POP_JUMP_IF_FALSE       10

  3           4 LOAD_CONST               1 (1)
              6 STORE_FAST               1 (y)
              8 JUMP_FORWARD             4 (to 14)

  5     >>   10 LOAD_CONST               2 (2)
             12 STORE_FAST               1 (y)

  6     >>   14 LOAD_FAST                1 (y)
             16 RETURN_VALUE
>>>
```

In the last post, we lifted the control structure out of the linear stream of
bytes and made a CFG:

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

By the end of this post, we will remove the stack and transform the code into
SSA:

```
bb0:
  v0 = LOAD_FAST 0
  POP_JUMP_IF_FALSE v0, bb2
bb1:
  v1 = LOAD_CONST 1
  JUMP_FORWARD bb3
bb2:
  v2 = LOAD_CONST 2
bb3:
  v3 = PHI v1 v2
  RETURN_VALUE v3
```

The important change here is the disappearance of the stack and the appearance
of virtual registers (`v0`, `v1`, etc). Also notable is the new `PHI`
instruction. We'll talk more about this later.

## What is abstract interpretation anyway?

If interpretation is turning programs into values, abstract interpretation must
be something else. Wikipedia's definition, while fancy sounding, is extremely
unhelpful to the newcomer:

> In computer science, abstract interpretation is a theory of sound
> approximation of the semantics of computer programs, based on monotonic
> functions over ordered sets, especially lattices.

Several times now I have gotten that far (one sentence) into the article before
dramatically sighing and closing the tab. Which is a shame, because I think the
next sentence is so much clearer:

> It can be viewed as a partial execution of a computer program which gains
> information about its semantics (e.g., control-flow, data-flow) without
> performing all the calculations.

Look at that! We apparently did some abstract interpretation in the last post
when we pulled control-flow out of the bytecode. If we turn the implicit stack
into virtual registers, we will have done some data-flow analysis, which is
also abstract interpretation. And if we combine the two, using the basic block
structure and the virtual registers to give each operation a unique name, we
have made constructed SSA by doing abstract interpretation.

If you are still not sure what abstract interpretation is, I encourage you to
read on anyway. This post might help.

## Removing the stack

Let's write a simple interpreter. We're going to use placeholder values (we'll
call them `Instruction`s) instead of numbers and stuff. We'll give the eval
function both the original Python code object as well as the new block
structure from the last post. We'll want access to the constant pool, local
variable names, and so on. It'll return us a list of these placeholder values
(`Instruction`s), one for each operation.

```python
def eval(code: CodeType, block: Block) -> List[Instruction]:
  # TODO: Implement
  pass
```

Since we're dealing with stack-based bytecode, we'll need a stack.

```python
def eval(code: CodeType, block: Block) -> List[Instruction]:
  stack: List[Instruction] = []
```

We'll also need the usual loop over instructions. Since we're only looking at
a basic block right now (no control flow), we don't even need an instruction
pointer or anything fancy like that. Just a for-each:

```python
def eval(code: CodeType, block: Block) -> List[Instruction]:
  stack: List[Instruction] = []
  for instr in block.bytecode:
    # TODO: implement
```

I haven't actually told you what we're computing yet, so let's pause and invent
this new placeholder value. The placeholder value is similar to the
`BytecodeOp` from the last post: it describes one operation. Unlike the last
post, each instruction keeps track of its own operands instead of being
"point-free": if we're going to remove the stack, the operands have to live
somewhere.

```python
class Instruction:
  def __init__(self, opcode: str, operands: List["Instruction"]):
    self.opcode: str = opcode
    self.operands: List[Instruction] = operands
```

We'll start off by implenenting a simple opcode: `LOAD_CONST`. `LOAD_CONST`
gets emitted when the bytecode compiler sees numbers, strings, and other
literals:

```python
def boring():
  return 123
#  0 LOAD_CONST               1 (123)
#  2 RETURN_VALUE
```

Each unique constant value gets put into this array called `co_consts` in the
code object, and `LOAD_CONST` is given an oparg that indexes into the array.

Then, at run-time in the real interpreter, the opcode handler takes the
`PyObject*` from the constant pool that corresponds to the oparg and pushes it
on the stack. We'll do something similar.

We need a place to store the actual constant object, so we'll make a subclass
of `Instruction` and store it there.

```python
class LoadConst(Instruction):
  def __init__(self, obj):
    super().__init__("LOAD_CONST", [])
    self.obj = obj
```

This is because `LOAD_CONST` doesn't actually have any operands that it takes
from the stack. It instead takes an index from its oparg and indexes into the
constant pool.

```python
def eval(code: CodeType, block: Block) -> List[Instruction]:
  stack: List[Instruction] = []
  for instr in block.bytecode:
    if instr.op == Op.LOAD_CONST:
      obj = code.co_consts[instr.arg]
      stack.append(LoadConst(obj))
    else:
      raise NotImplementedError("unknown opcode")
```

Just modeling the stack is fine but we do want to keep track of all of these
intermediate results, so let's make a list of all the values we create.

```python
def eval(code: CodeType, block: Block) -> List[Instruction]:
  stack: List[Instruction] = []
  result: List[Instruction] = []
  for instr in block.bytecode:
    if instr.op == Op.LOAD_CONST:
      obj = code.co_consts[instr.arg]
      instr = LoadConst(obj)
      stack.append(instr)
      result.append(instr)
    else:
      raise NotImplementedError("unknown opcode")
  return result
```

Let's try with another opcode: `BINARY_ADD`. People add numbers, right? Sounds
useful. To refresh, `BINARY_ADD` takes two operands from the stack, adds them
together, and then pushes the result back onto the stack.

```python
def eval(code: CodeType, block: Block) -> List[Instruction]:
  stack: List[Instruction] = []
  result: List[Instruction] = []
  for instr in block.bytecode:
    # ...
    elif instr.op == Op.BINARY_ADD:
      right = stack.pop()
      left = stack.pop()
      instr = Instruction("BINARY_ADD", [left, right])
      stack.append(instr)
      result.append(instr)
    else:
      raise NotImplementedError("unknown opcode")
  return result
```

You can kind of see where this is going, if you squint. Like the last post, we
are taking the linear nature of bytecode and extracting structure from it.
Where before we had to imagine a stack for our code, now we have a tree
structure with pointers:

```
            add
          /     \
       left     right
```

Kind of looks like an abstract syntax tree in this shape, but people tend to
instead think about it still in its linear form[^sea-of-nodes]... but with
names. Something like:

[^sea-of-nodes]: Except for the Sea of Nodes people, for whom everything is a
    big instruction soup/graph. There does not appear to be an industry or
    academic consensus for which approach is better, but people often have very
    strong feelings one way or another.

```
v3 = LOAD_CONST 0
v4 = LOAD_CONST 1
v5 = BINARY_ADD v3, v4
```

That's it. You've removed the stack from stack-based bytecode by interpreting
just the stack (*not* the values) at compile-time. Please pat yourself on the
back.

## Local value numbering

SSA isn't just about virtual registers, though. As I mentioned offhandedly
before, it's also about giving each operation a unique name. Most people use
variables in their code, not just trees of values, so we have to figure out how
to model `LOAD_FAST` and `STORE_FAST` in our abstract interpretation.

Since all the names of local variables are known at bytecode compilation time,
the bytecode compiler assigns an index for each name and puts the names at
those indices in this field called `co_varnames`. Then, at run-time, CPython
models these local variables with an array, where instead each index
corresponds to a value. Sound familiar? It's kind of like constants.

Since we don't have any values handy, we will model each local with an
`Instruction`.

```python
def eval(code: CodeType, block: Block) -> List[Instruction]:
  # ...
  locals: List[Instruction] = [None] * code.co_nlocals
  for instr in block.bytecode:
    # ...
  return result
```

Let's take a look at some Python code:

```python
def wow_locals():
  x = 1
  return x
# 0 LOAD_CONST               1 (1)
# 2 STORE_FAST               0 (x)
# 4 LOAD_FAST                0 (x)
# 6 RETURN_VALUE
```

We can see our friend `LOAD_CONST` and now both `LOAD_FAST` and `STORE_FAST`
that read from and write to the locals array, respectively. `LOAD_FAST` reads
from the locals and pushes to the stack, whereas `STORE_FAST` reads (pops) from
the stack and writes to the locals.

```python
def eval(code: CodeType, block: Block) -> List[Instruction]:
  # ...
  locals: List[Instruction] = [None] * code.co_nlocals
  for instr in block.bytecode:
    # ...
    elif instr.op == Op.LOAD_FAST:
      stack.append(locals[instr.arg])
    elif instr.op == Op.STORE_FAST:
      locals[instr.arg] = stack.pop()
    # ...
  return result
```

You may notice that neither of these instructions need corresponding
`Instruction` objects. That's because they don't actually *do* anything: they
just name expressions like we are already doing.

### Redefining locals

You might be wondering about this whole "unique name" thing I keep pushing. We
haven't done any uniqueness checking at all, and most programming languages,
Python included, allow the programmer to redefine variables. What gives?

Well, let's see what happens if we redefine a local:

```python
def redefine():
  x = 123
  x = 456
  return x
#  0 LOAD_CONST               1 (123)
#  2 STORE_FAST               0 (x)
#  4 LOAD_CONST               2 (456)
#  6 STORE_FAST               0 (x)
#  8 LOAD_FAST                0 (x)
# 10 RETURN_VALUE
```

The bytecode writes to the locals array each time. Our abstract interpreter
does the same. This means that we will only ever store a reference to the most
recently written `Instruction`. Then, when we read the locals, we find that
reference:

```
v0 = LOAD_CONST 1
v1 = LOAD_CONST 2
RETURN_VALUE v1
```

We have the right constant---the second one---the number `456`. This technique
is called "local value numbering". The implementation is so subtle that it took
me some time to understand.

### Putting it together

Let's look at a very slightly bigger example code snippet to see what our
abstract interpreter gives us. Nothing scary---no control flow---just
constants, local variables, adding numbers, and vibes.

```python
def adding_with_names():
  x = 1
  y = 2
  return x + y
#  0 LOAD_CONST               1 (1)
#  2 STORE_FAST               0 (x)
#  4 LOAD_CONST               2 (2)
#  6 STORE_FAST               1 (y)
#  8 LOAD_FAST                0 (x)
# 10 LOAD_FAST                1 (y)
# 12 BINARY_ADD
# 14 RETURN_VALUE
```

At this point we should have "evaluated away" the both the stack and local
variable names, which means we should get something pretty compact. Running
this code through our abstract interpreter gives:

```
v0 = LOAD_CONST 1
v1 = LOAD_CONST 2
v2 = BINARY_ADD v0, v1
RETURN_VALUE v2
```

Which means that we have successfully folded away both the stack and local
variables. Find a friend and get them to applaud you.

## Global value numbering

## Loops

<br />
<hr style="width: 100px;" />
<!-- Footnotes -->

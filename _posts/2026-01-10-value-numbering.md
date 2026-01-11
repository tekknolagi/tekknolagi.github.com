---
title: "Value numbering"
layout: post
---

Or, *But my values. They already have numbers!*

Static single assignment (SSA) gives names to values: every expression has a
name, and each name corresponds to exactly one expression. It transforms
programs like this:

```python
x = 0
x = x + 1
x = x + 1
```

into

```python
v0 = 0
v1 = v0 + 1
v2 = v1 + 1
```

It's great because it makes clear the differences between the two `x + 1`
expressions. Though they textually look similar, they compute different values.
The first computes 1 and the second computes 2. In this example, it is not
possible to substitute in a variable and re-use the value of `x + 1`, because
the `x`s are different.

But what if we see two "textually" identical instructions in SSA? That sounds
much more promising than non-SSA because we have removed (much of) the
statefulness of it all. When can we re-use the result?

## Local value numbering

Let's extend the above IR snippet with two more instructions, v3 and v4.

```python
v0 = 0
v1 = v0 + 1
v2 = v1 + 1
v3 = v0 + 1  # new
v4 = do_something(v2, v3)
```

In this new snippet, v3 looks the same as v1: adding v0 and 1. Assuming our
addition operation is some ideal mathematical addition, we can absolutely
re-use v1; no need to compute the addition again. We can rewrite the IR to
something like:

```python
v0 = 0
v1 = v0 + 1
v2 = v1 + 1
v3 = v1
v4 = do_something(v2, v3)
```

This is kind of similar to the destructive union-find representation that
JavaScriptCore and a couple other compilers use, where the optimizer doesn't
eagerly re-write all uses but instead leaves a little breadcrumb
`Identity`/`Assign` instruction.

We could then run our copy propagation pass ("union-find cleanup"?) and get:

```python
v0 = 0
v1 = v0 + 1
v2 = v1 + 1
v4 = do_something(v2, v1)
```

Great. But how does this happen? How does an optimizer identify reusable
instruction candidates that are "textually identical"? Generally, there is [no
actual text in the
IR](https://pointersgonewild.com/2011/10/07/optimizing-global-value-numbering/).

One popular solution is to compute a hash of each instruction. Then any
instructions with the same hash (that also compare equal, in case of
collisions) are considered equivalent.

I particularly like the Maxine VM implementation. For example, here is the
`valueNumber` implementation for most binary operations, slightly modified for
clarity:

```java
public abstract class Instruction extends Value { ... }

// The base class for binary operations
public abstract class Op2 extends Instruction {
    // Each binary operation has an opcode and two opearands
    public final int opcode;  // (IMUL, IADD, ...)
    Value x;
    Value y;

    @Override
    public int valueNumber() {
        // There are other fields but only opcode, and operands get hashed.
        // Always set at least one bit in case the hash wraps to zero.
        return 0x20000000
        | (opcode
           + 7  * System.identityHashCode(x)
           + 11 * System.identityHashCode(y));
    }

    @Override
    public boolean valueEqual(Instruction i) {
        if (i instanceof Op2) {
            Op2 o = (Op2) i;
            return opcode == o.opcode && x == o.x && y == o.y;
        }
        return false;
    }
}
```

The value numbering implementation assumes that if a `valueNumber`
function returns 0, it does not wish to be considered for value
numbering. Why might an instruction opt-out of value numbering?

## Pure vs impure

Some instructions are not "pure". Purity is in the eye of the beholder, but in
general it means that an instruction does not interact with the state of the
outside world, except for trivial computation on its operands.

A load from an array object is not a pure operation. The load operation
implicitly relies on the state of the memory. In addition, in some runtime
systmes, the load might raise an exception. Changing the source location where
an exception is raised is generally frowned upon. Languages such as Java often
have this requirement codified in their specifications.

## Local value numbering

```java
// Local value numbering
BlockBegin block = ...;
ValueMap currentMap = new ValueMap();
InstructionSubstituter subst = new InstructionSubstituter();

// visit all instructions of this block
for (Instruction instr = block.next(); instr != null; instr = instr.next()) {
    // attempt value numbering (uses valueNumber() and valueEqual())
    //
    // return a previous instruction if it exists in the map, or insert the
    // current instruction into the map and return it
    Instruction f = currentMap.findInsert(instr);
    if (f != instr) {
        // remember the replacement in the union-find
        subst.setSubst(instr, f);
    }
}
```

<!--
## Equivalence classes
-->

## State management and invalidation

## Global value numbering

## Acyclic e-graphs

https://www.cs.cornell.edu/courses/cs6120/2019fa/blog/global-value-numbering/

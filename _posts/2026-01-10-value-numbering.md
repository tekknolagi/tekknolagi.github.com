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

Identifying instructions that have the same value is called *value numbering*.

## Eliminating common subexpressions

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
`Identity`/`Assign` instruction[^cinder].

[^cinder]: Writing this post is roughly the time when I realized that the whole
    time I was wondering why Cinder did not use union-find for rewriting, it
    actually did! Optimizing instruction `X = A + 0` by replacing with `X =
    Assign A` followed by copy propagation is equivalent to union-find.

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

I particularly like the [Maxine VM][maxine] implementation. For example, here is the
`valueNumber` implementation for most binary operations, slightly modified for
clarity:

[maxine]: https://maxine-vm.readthedocs.io/en/stable/

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
have requirements about where exceptions are raised codified in their
specifications.

We'll work only on pure operations for now, but we'll come back to this later.
We do often want to optimize impure operations as well!

## Local value numbering

Let's build a small implementation of value numbering. We'll start with
straight-line code---no branches or anything tricky.

Most compiler optimizations on control-flow graphs (CFGs) iterate over the
instructions "top to bottom"[^order] and it seems like we can do the same thing
here too.

From what we've seen so far optimizing our made-up IR snippet, we can do
something like this:

* initialize a map from instruction numbers to instruction pointers
* for each instruction `i`
  * if `i` wants to participate in value numbering
    * if `i`'s value number is already in the map, replace all pointers to `i`
      in the rest of the program with the corresponding value from the map
    * otherwise, add `i` to the map

The find-and-replace, remember, is not a literal find-and-replace, but instead
something like:

```python
instr.opcode = "Assign"
instr.operands[0] = replacement
```

or

```python
instr.make_equal_to(replacement)
```

(if you have been following along with the [toy optimizer][toy] series)

[toy]: https://pypy.org/categories/toy-optimizer.html

This several-line function (as long as you already have a hashmap and a
union-find available to you) is enough to build local value numbering! And real
compilers are built this way, too.

If you don't believe me, take a look at this slightly edited snippet from
[Maxine's][maxine] value numbering implementation. It has all of the components
we just talked about: iterating over instructions, map lookup, and some
substitution.

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

This alone will get you pretty far. Code generators of all shapes tend to leave
messy repeated computations all over their generated code and this will make
short work of them.

Sometimes, though, your computations are spread across control flow---over
multiple basic blocks. What do you do then?

<!--
## Equivalence classes
-->

## Global value numbering

Let's tackle control flow case by case.

First is the simple case from above: one block. In this case, we can go top to
bottom with our value numbering and do alright.

<figure>
  <object class="svg" type="image/svg+xml" data="/assets/img/gvn-one-block.svg"></object>
</figure>

Second is also reasonable to handle: one block flowing into another. In this
case, we can still go top to bottom. We just have to find a way to iterate over
the blocks.

If we're not going to share value maps between blocks, the order doesn't
matter. But since the point of global value numbering is to share values, we
have to iterate them in topological order (reverse post order (RPO)). This
ensures that predecessors get visited before successors. If you have `bb0 ->
bb1`, we have to visit first `bb0` and then `bb1`.

Because of how SSA works and how CFGs work, the second block can "look up" into
the first block and use the values from it. To get global value numbering
working, we have to copy `bb0`'s value map before we start processing `bb1` so
we can re-use the instructions.

<figure>
  <object class="svg" type="image/svg+xml" data="/assets/img/gvn-two-blocks.svg"></object>
</figure>

Maybe something like:

```python
value_map = ValueMap()
for block in function.reverse_post_order():
    local_value_numbering(block, value_map)
```

Then the expressions can accrue across blocks. `bb1` can re-use the
already-computed `Add v0, 1` from `bb0` because it is still in the map.

...but this breaks as soon as you have control-flow splits. Consider the
following shape graph:

<!--
digraph G {
  node [shape=square];
  A -> B;
  A -> C;
}
-->
<figure>
  <object class="svg" type="image/svg+xml" data="/assets/img/gvn-split.svg"></object>
</figure>

We're going to iterate over that graph in one of two orders: A B C or A C B. In
either case, we're going to be adding all this stuff into the value map from
one block (say, A) that is not actually available to its sibling block (say,
B).

When I say "not available", I mean "would not have been computed before". This
is because we execute either A then B or A then C. There's no world in which we
execute B then C.

But alright, look at a third case where there is such a world: a control-flow
join. In this diagram, we have two predecessor blocks B and C each flowing into
D. In this diagram, B *always* flows into D and also C *always* flows into D.
So the iterator order is fine, right?

<!--
digraph G {
  node [shape=square];
  A -> B;
  A -> C;
  B -> D;
  C -> D;
}
-->
<figure>
  <object class="svg" type="image/svg+xml" data="/assets/img/gvn-join.svg"></object>
</figure>

Well, still no. We have the same sibling problem as before.

We also have a weird question when we enter D: where did we come from? If we
came from B, we can re-use expressions from B. If we came from C, we can re-use
expressions from C. But we cannot in general know which predecessor block we
came from.

The only block we know *for sure* that we executed before D is A.

```java
public class GlobalValueNumberer {
    final HashMap<BlockBegin, ValueMap> valueMaps;
    final InstructionSubstituter subst;
    ValueMap currentMap;

    public GlobalValueNumberer(IR ir) {
        this.subst = new InstructionSubstituter(ir);
        // reverse post-order
        List<BlockBegin> blocks = ir.linearScanOrder();
        valueMaps = new HashMap<BlockBegin, ValueMap>(blocks.size());
        optimize(blocks);
        subst.finish();
    }

    void optimize(List<BlockBegin> blocks) {
        int numBlocks = blocks.size();
        BlockBegin startBlock = blocks.get(0);

        // initial value map, with nesting 0
        valueMaps.put(startBlock, new ValueMap());

        for (int i = 1; i < numBlocks; i++) {
            // iterate through all the blocks
            BlockBegin block = blocks.get(i);
            BlockBegin dominator = block.dominator();

            // create new value map with increased nesting
            currentMap = new ValueMap(valueMaps.get(dominator));

            // << INSERT LOCAL VALUE NUMBERING HERE >>

            // remember value map for successors
            valueMaps.put(block, currentMap);
        }
    }
}
```

## State management and invalidation

MemoryMap and GraphBuilder

## DVNT and path back to dominator

## Acyclic e-graphs

https://www.cs.cornell.edu/courses/cs6120/2019fa/blog/global-value-numbering/

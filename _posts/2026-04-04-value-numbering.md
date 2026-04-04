---
title: "Value numbering"
layout: post
---

Welcome back to compiler land. Today we're going to talk about *value
numbering*, which is like SSA, but more.

Static single assignment (SSA) gives names to values: every expression has a
name, and each name corresponds to exactly one expression. It transforms
programs like this:

```python
x = 0
x = x + 1
x = x + 1
```

where the variable `x` is assigned more than once in the program text, into
programs like this:

```python
v0 = 0
v1 = v0 + 1
v2 = v1 + 1
```

where each assignment to `x` has been replaced with an assignment to a new
fresh name.

It's great because it makes clear the differences between the two `x + 1`
expressions. Though they textually look similar, they compute different values.
The first computes 1 and the second computes 2. In this example, it is not
possible to substitute in a variable and re-use the value of `x + 1`, because
the `x`s are different.

But what if we see two "textually" identical instructions in SSA? That sounds
much more promising than non-SSA because the transformation into SSA form has
removed (much of) the statefulness of it all. When can we re-use the result?

Identifying instructions that are known at compile-time to always produce the
same value at run-time is called *value numbering*. <!-- This is also called common
subexpression elimination (CSE), though for some reason the two mean slightly
different things to different groups of people. -->

## Eliminating common subexpressions

To understand value numbering, let's extend the above IR snippet with two more
instructions, v3 and v4.

```python
v0 = 0
v1 = v0 + 1
v2 = v1 + 1
v3 = v0 + 1  # new
v4 = do_something(v2, v3)  # new
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

When trying to figure all this out, I read through a couple of different
implementations. I particularly like the [Maxine VM][maxine] implementation.
For example, here is the `valueNumber` (hashing) and `valueEqual`
functions for most binary operations, slightly modified for clarity:

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

The rest of the value numbering implementation assumes that if a `valueNumber`
function returns 0, it does not wish to be considered for value
numbering. Why might an instruction opt-out of value numbering?

## Pure vs impure

An instruction might opt out of value numbering if it is not "pure".

Some instructions are not pure. Purity is in the eye of the beholder, but in
general it means that an instruction does not interact with the state of the
outside world, except for trivial computation on its operands. (What does it
mean to de-duplicate/cache/reuse `printf`?)

A load from an array object is also not a pure operation[^heap-ssa]. The load operation
implicitly relies on the state of the memory. Also, even if the array was
known-constant, in some runtime
systems, the load might raise an exception. Changing the source location where
an exception is raised is generally frowned upon. Languages such as Java often
have requirements about where exceptions are raised codified in their
specifications.

[^heap-ssa]: In some forms of SSA, like heap-array SSA or sea of nodes, it's
    possible to more easily de-duplicate loads because the memory
    representation has been folded into (modeled in) the IR.

We'll work only on pure operations for now, but we'll come back to this later.
We do often want to optimize impure operations as well!

We'll start off with the simplest form of value numbering, which operates only
on linear sequences of instructions, like basic blocks or traces.

## Local value numbering

Let's build a small implementation of local value numbering (LVN). We'll start with
straight-line code---no branches or anything tricky.

Most compiler optimizations on control-flow graphs (CFGs) iterate over the
instructions "top to bottom"[^order] and it seems like we can do the same thing
here too.

[^order]: The order is a little more complicated than that: [reverse
    post-order](https://stackoverflow.com/questions/36131500/what-is-the-reverse-postorder)
    (RPO). And there's a paper called "A Simple Algorithm for Global Data Flow
    Analysis Problems" that I don't yet have a PDF for that claims that RPO is
    optimal for solving dataflow problems.

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

This several-line function (as long as you already have a hash map and a
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

Computing value numbers for an entire function is called *global value
numbering* (GVN) and it requires dealing with control flow (if, loops, etc). I
don't just mean that for an entire function, we run local value numbering
block-by-block. Global value numbering implies that expressions can be
de-duplicated and shared across blocks.

Let's tackle control flow case by case.

First is the simple case from above: one block. In this case, we can go top to
bottom with our value numbering and do alright.

<figure>
  <object class="svg" type="image/svg+xml" data="/assets/img/gvn-one-block.svg"></object>
</figure>

The second case is also reasonable to handle: one block flowing into another. In this
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
one block (say, B) that is not actually available to its sibling block (say,
C).

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

Well, still no. We have the same sibling problem as before. B and C still can't
share value maps.

We also have a weird question when we enter D: where did we come from? If we
came from B, we can re-use expressions from B. If we came from C, we can re-use
expressions from C. But we cannot in general know which predecessor block we
came from.

The only block we know *for sure* that we executed before D is A. This means we
can re-use A's value map in D because we can guarantee that all execution paths
that enter D have previously gone through A.

This relationship is called a *dominator* relationship and this is the key to
one style of global value numbering that we're going to talk about in this
post[^other-gvn]. A block can always use the value map from any other block
that dominates it.

[^other-gvn]: TODO write about equivalence class GVN and dataflow based GVN
    see [paper](/assets/img/briggs-gvn.pdF) (PDF)

For completeness' sake, in the diamond diagram, A dominates each of B and C,
too.

We can compute dominators a couple of ways[^compute-doms], but that's a little
bit out of scope for this blog post. If we assume that we have dominator
information available in our CFG, we can use that for global value numbering.
And that's just what---you guessed it---Maxine VM does.

[^compute-doms]: There's the iterative dataflow way,
    [Lengauer-Tarjan](/assets/img/dominators-lengauer-tarjan.pdf) (PDF), the
    [Engineered Algorithm](/assets/img/dominators-engineered.pdf) (PDF),
    [hybrid/Semi-NCA approach](/assets/img/dominators-practice.pdf) (PDF), ...

It iterates over all blocks in reverse post-order, doing local value numbering,
threading through value maps from dominator blocks. In this case, their method
`dominator` gets the *immediate dominator*: the "closest" dominator block of
all the blocks that dominate the current one.

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

And that's it! That's the core of Maxine's [GVN implementation][maxine-gvn]. I
love how short it is. For not very much code, you can remove a lot of duplicate
pure SSA instructions.

[maxine-gvn]: https://github.com/beehive-lab/Maxine-VM/blob/e213a842f78983e2ba112ae46de8c64317bc206e/com.sun.c1x/src/com/sun/c1x/opt/GlobalValueNumberer.java

But what if we want to handle impure instructions?

## State management and invalidation

Languages such as Java allow for reading fields from the `this`/`self` object within
methods as if the field were a variable name. This makes code like the
following common:

```java
class CPU {
    private void exec_adc() {
        int result_int = regA + fetched_data + flagCARRY;
        byte result = (byte) result_int;
        // ...
        int a = result_int ^ regA;
        int b = result_int ^ fetched_data;
        // ...
        regA = result;
    }
}
```

Each of these reference to `regA` and `fetched_data` is an implicit reference
to `this.regA` or `this.fetched_data`, which is semantically a field load off
an object. You can see it in [the bytecode][cpu-bytecode] (thanks, Matt Godbolt):

[cpu-bytecode]: https://godbolt.org/#g:!((g:!((g:!((h:codeEditor,i:(filename:'1',fontScale:14,fontUsePx:'0',j:1,lang:java,selection:(endColumn:19,endLineNumber:14,positionColumn:19,positionLineNumber:14,selectionStartColumn:19,selectionStartLineNumber:14,startColumn:19,startLineNumber:14),source:'class+CPU+%7B%0A++++private+void+exec_adc()+%7B%0A++++++++int+result_int+%3D+regA+%2B+fetched_data+%2B+flagCARRY%3B%0A++++++++byte+result+%3D+(byte)+result_int%3B%0A++++++++//+...%0A++++++++int+a+%3D+result_int+%5E+regA%3B%0A++++++++int+b+%3D+result_int+%5E+fetched_data%3B%0A++++++++//+...%0A++++++++regA+%3D+result%3B%0A++++%7D%0A%0A++++int+regA%3B%0A++++int+fetched_data%3B%0A++++int+flagCARRY%3B%0A%7D%0A'),l:'5',n:'0',o:'Java+source+%231',t:'0')),k:50,l:'4',n:'0',o:'',s:0,t:'0'),(g:!((h:compiler,i:(compiler:java2501,filters:(b:'0',binary:'1',binaryObject:'1',commentOnly:'0',debugCalls:'1',demangle:'0',directives:'0',execute:'1',intel:'0',libraryCode:'0',trim:'1',verboseDemangling:'0'),flagsViewOpen:'1',fontScale:14,fontUsePx:'0',j:1,lang:java,libs:!(),options:'',overrides:!(),selection:(endColumn:19,endLineNumber:40,positionColumn:1,positionLineNumber:1,selectionStartColumn:19,selectionStartLineNumber:40,startColumn:1,startLineNumber:1),source:1),l:'5',n:'0',o:'+jdk+25.0.1+(Editor+%231)',t:'0')),k:50,l:'4',n:'0',o:'',s:0,t:'0')),l:'2',n:'0',o:'',t:'0')),version:4

```
class CPU {
  int regA;

  int fetched_data;

  int flagCARRY;

  CPU();
         0: aload_0
         1: invokespecial #1                  // Method java/lang/Object."<init>":()V
         4: return


  private void exec_adc();
         0: aload_0
         1: getfield      #7                  // Field regA:I
         4: aload_0
         // ...
        20: getfield      #7                  // Field regA:I
        23: ixor
        24: istore_3
        25: iload_1
        26: aload_0
        27: getfield      #13                 // Field fetched_data:I
        30: ixor
        31: istore        4
        33: aload_0
        34: iload_2
        35: putfield      #7                  // Field regA:I
        38: return
}
```

When straightforwardly building an SSA IR from the JVM bytecode for this
method, you will end up with a bunch of IR that looks like this:

```
v0 = LoadField self, :regA
v1 = LoadField self, :fetched_data
v2 = LoadField self, :flagCARRY
v3 = IntAdd v0, v1
v4 = IntAdd v3, v2
// ...
v7 = LoadField self, :regA
v8 = IntXor v4, v7
v9 = LoadField self, :fetched_data
v10 = IntXor v4, v9
// ...
StoreField self, :regA, ...
```

Pretty much the same as the bytecode. Even though no code in the middle could
modify the field `regA` (which would require a re-load), we still have a
duplicate load. Bummer.

I don't want to re-hash this too much but it's possible to fold [Load and store
forwarding](/blog/toy-load-store/) into your GVN implementation by either:

* doing load-store forwarding as part of local value numbering and clearing
  memory information from the value map at the end of each block, or
* keeping track of effects across blocks

See, there's nothing fundamentally stopping you from tracking the state of your
heap at compile-time across blocks. You just have to do a little more
bookkeeping. In our dominator-based GVN implementation, for example, you can:

1. track heap write effects for each block
1. at the start of each block B, union all of the "kill" sets for every block
   back to its immediate dominator
1. finally, remove the stuff that got killed from the dominator's value map

Not so bad.

Maxine doesn't do global memory tracking, but they do a limited form of
load-store forwarding while building their HIR from bytecode: see
[GraphBuilder] which uses the [MemoryMap] to help track this stuff. At least
they would not have the same duplicate `LoadField` instructions in the example
above!

<!--
```ruby
module Psych
  module Visitors
    class YAMLTree < Psych::Visitors::Visitor
      def initialize emitter, ss, options
        # ...
        @line_width = options[:line_width]
        if @line_width && @line_width < 0
          if @line_width == -1
            # Treat -1 as unlimited line-width, same as libyaml does.
            @line_width = nil
          else
            fail(...)
          end
        end
        # ...
    end
  end
end
```
-->

[MemoryMap]: https://github.com/beehive-lab/Maxine-VM/blob/e213a842f78983e2ba112ae46de8c64317bc206e/com.sun.c1x/src/com/sun/c1x/graph/MemoryMap.java
[GraphBuilder]: https://github.com/beehive-lab/Maxine-VM/blob/e213a842f78983e2ba112ae46de8c64317bc206e/com.sun.c1x/src/com/sun/c1x/graph/GraphBuilder.java#L871

We've now looked at one kind of value numbering and one implementation of it.
What else is out there?

## Out in the world

Apparently, you can get better results by having a unified hash table (p9 of
[Briggs GVN](/assets/img/briggs-gvn.pdf)) of expressions, not limiting the
value map to dominator-available expressions. Not 100% on how this works yet.
<!-- TODO What do you do in the second pass for available expressions? -->

There's also a totally different kind of value numbering called value
partitioning (p12 of [Briggs GVN](/assets/img/briggs-gvn.pdf)). See also a nice
blog post about this by Allen Wang from the [Cornell compiler
course](https://www.cs.cornell.edu/courses/cs6120/2025sp/blog/global-value-numbering/).
I think this mostly replaces the hashing bit, and you still need some other
thing for the available expressions bit.

Ben Titzer and Seth Goldstein have some good [slides from
CMU](https://www.cs.cmu.edu/~411/slides/s25-24-gvn-inlining.pdf). Where they
talk about the worklist dataflow approach. Apparently this is slower but gets
you more available expressions than just looking to dominator blocks. I wonder
how much it differs from dominator+unified hash table.

While Maxine uses hash table cloning to copy value maps from dominator blocks,
there are also compilers such as Cranelift that use
[scoped hash maps](https://github.com/bytecodealliance/wasmtime/blob/main/cranelift/codegen/src/scoped_hash_map.rs)
to track this information more efficiently. (Though [Amanieu
notes](https://github.com/bytecodealliance/wasmtime/issues/4371#issuecomment-1255956651) that you may
not need a scoped hash map and instead can tag values in your value map with the
block they came from, ignoring non-dominating values with a quick check. I
haven't internalized this yet.)

## Wrapping up

Go forth and give your values more numbers.

<!--
## Acyclic e-graphs

Commutativity; canonicalization

Seeding alternative representations into the GVN

Aegraphs and union-find during GVN

https://github.com/bytecodealliance/rfcs/blob/main/accepted/cranelift-egraph.md
https://github.com/bytecodealliance/wasmtime/issues/9049
https://github.com/bytecodealliance/wasmtime/issues/4371

## PRE

https://www.cs.cornell.edu/courses/cs6120/2019fa/blog/global-value-numbering/
-->

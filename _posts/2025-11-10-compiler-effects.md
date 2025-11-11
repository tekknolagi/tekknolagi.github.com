---
title: A catalog of side effects
layout: post
---

Optimizing compilers like to keep track of each IR instruction's *effects*. An
instruction's effects vary wildly from having no effects at all, to writing a
specific variable, to completely unknown (writing all state).

This post can be thought of as a continuation of [What I talk about when I talk
about IRs](/blog/irs/), specifically the section talking about asking the right
questions. When we talk about effects, we should ask the right questions: not
*what opcode is this?* but instead *what effects does this opcode have?*

Different compilers represent and track these effects differently. I've been
thinking about how to represent these effects all year, so I have been doing
some reading. In this post I will give some summaries of the landscape of
approaches. Please feel free to suggest more.

## Some background

Internal IR effect tracking is similar to the programming language notion of
algebraic effects in type systems, but internally, compilers keep track of
finer-grained effects. Effects such as "writes to a local variable", "writes to
a list", or "reads from the stack" indicate what instructions can be
re-ordered, duplicated, or removed entirely.

For example, consider the following pseodocode for some made-up language that
stands in for a snippet of compiler IR:

```python
# ...
v = some_var[0]
another_var[0] = 5
# ...
```

The goal of effects is to communicate to the compiler if, for example, these two IR
instructions can be re-ordered. The second instruction *might* write to a
location that the first one reads. But it also might not! This is about knowing
if `some_var` and `another_var` *alias*---if they are different names that
refer to the same object.

We can sometimes answer that question directly, but often it's cheaper to
compute an approximate answer: *could* they even alias? It's possible that
`some_var` and `another_var` have different types, meaning that (as long as you
have strict aliasing) the `Load` and `Store` operations that implement these
reads and writes by definition touch different locations. And if they look
at disjoint locations, there need not be any explicit order enforced.

Different compilers keep track of this information differently. The null effect
analysis gives up and says "every instruction is maximally effectful" and
therefore "we can't re-order or delete any instructions". That's probably fine
for a first stab at a compiler, where you will get a big speed up purely based
on strength reductions. Over-approximations of effects should always be
valid.

But at some point you start wanting to do dead code elimination (DCE), or
common subexpression elimination (CSE), or move instructions around, and you
start wondering how to represent effects. That's where I am right now. So
here's a catalog of different compilers I have looked at recently.

There are two main ways I have seen to represent effects: bitsets and heap
range lists. We'll look at one example compiler for each, talk a bit about
tradeoffs, then give a bunch of references to other major compilers.

We'll start with [Cinder][cinder], a Python JIT, because that's what I used to
work on.

[cinder]: https://github.com/facebookincubator/cinder

## Cinder

[Cinder][cinder] tracks heap effects for its high-level IR (HIR) in
[instr_effects.h][cinder-instr-effects-h]. Pretty much everything happens in
the `memoryEffects(const Instr& instr)` function, which is expected to know
everything about what effects the given instruction might have.

[cinder-instr-effects-h]: https://github.com/facebookincubator/cinderx/blob/8bf5af94e2792d3fd386ab25b1aeedae27276d50/cinderx/Jit/hir/instr_effects.h

The data representation is a bitset representation of a lattice called an
`AliasClass` and that is defined in [alias_class.h][cinder-alias-class-h]. Each
bit in the bitset represents a distinct location in the heap: reads from and
writes to each of these locations are guaranteed not to affect any of the other
locations.

[cinder-alias-class-h]: https://github.com/facebookincubator/cinderx/blob/8bf5af94e2792d3fd386ab25b1aeedae27276d50/cinderx/Jit/hir/alias_class.h

Here is the X-macro that defines it:

```c
#define HIR_BASIC_ACLS(X) \
  X(ArrayItem)            \
  X(CellItem)             \
  X(DictItem)             \
  X(FuncArgs)             \
  X(FuncAttr)             \
  X(Global)               \
  X(InObjectAttr)         \
  X(ListItem)             \
  X(Other)                \
  X(TupleItem)            \
  X(TypeAttrCache)        \
  X(TypeMethodCache)

enum BitIndexes {
#define ACLS(name) k##name##Bit,
    HIR_BASIC_ACLS(ACLS)
#undef ACLS
};
```

Note that each bit implicitly represents a set: `ListItem` does not refer to a
*specific* list index, but the infinite set of all possible list indices. It's
*any* list index. Still, every list index is completely disjoint from, say, every
entry in a global variable table.

(And, to be clear, an object in a list might be the same as an object in a
global variable table. The objects themselves can alias. But the thing being
written to or read from, the thing *being side effected*, is the container.)

Like other bitset lattices, it's possible to union the sets by or-ing the bits.
It's possible to query for overlap by and-ing the bits.

```c++
class AliasClass {
  // The union of two AliasClass
  AliasClass operator|(AliasClass other) const {
    return AliasClass{bits_ | other.bits_};
  }

  // The intersection (overlap) of two AliasClass
  AliasClass operator&(AliasClass other) const {
    return AliasClass{bits_ & other.bits_};
  }
};
```

If this sounds familiar, it's because (as the repo notes) it's a similar idea
to Cinder's [type lattice representation](/blog/lattice-bitset/).

Like other lattices, there is both a bottom element (no effects) and a top
element (all possible effects):

```c
#define HIR_OR_BITS(name) | k##name

#define HIR_UNION_ACLS(X)                           \
  /* Bottom union */                                \
  X(Empty, 0)                                       \
  /* Top union */                                   \
  X(Any, 0 HIR_BASIC_ACLS(HIR_OR_BITS))             \
  /* Memory locations accessible by managed code */ \
  X(ManagedHeapAny, kAny & ~kFuncArgs)
```

Union operations naturally hit a fixpoint at `Any` and intersection operations
naturally hit a fixpoint at `Empty`.

All of this together lets the optimizer ask and answer questions such as:

* where might this instruction write?
* (because CPython is reference counted and incref implies ownership) where
  does this instruction borrow its input from?
* do these two instructions' write destinations overlap?

and more.

Let's take a look at an (imaginary) IR version of the code snippet in the intro
and see what analyzing it might look like in the optimizer. Here is the fake
IR:

```
v0: Tuple = ...
v1: List = ...
v2: Int[5] = ...
# v = some_var[0]
v3: Object = LoadTupleItem v0, 0
# another_var[0] = 5
StoreListItem v1, 0, v2
```

You can imagine that `LoadTupleItem` declares that it reads from the
`TupleItem` heap and `StoreListItem` declares that it writes to the `ListItem`
heap. Because tuple and list pointers cannot be casted into one another and
therefore cannot alias, these are
disjoint heaps in our bitset. Therefore `ListItem & TupleItem == 0`, therefore
these memory operations can never interfere! They can (for example) be
re-ordered arbitrarily.

In Cinder, these memory effects could in the future be used for instruction
re-ordering, but they are today mostly used in two places: the refcount
insertion pass and DCE.

DCE involves first finding the set of instructions that need to be kept around
because they are useful/important/have effects. So here is what the Cinder DCE
`isUseful` looks like:

```c++
bool isUseful(Instr& instr) {
  return instr.IsTerminator() || instr.IsSnapshot() ||
      (instr.asDeoptBase() != nullptr && !instr.IsPrimitiveBox()) ||
      (!instr.IsPhi() && memoryEffects(instr).may_store != AEmpty);
}
```

There are some other checks in there but `memoryEffects` is right there at the
core of it!

Now that we have seen the bitset representation of effects and an
implementation in Cinder, let's take a look at a different representation and
and an implementation in JavaScriptCore.

## JavaScriptCore

I keep coming back to [How I implement SSA form][pizlo-ssa] by [Fil
Pizlo][pizlo], one of the significant contributors to JavaScriptCore (JSC). In
particular, I keep coming back to the [Uniform Effect
Representation][pizlo-effect] section. This notion of "abstract heaps" felt
very... well, abstract. Somehow more abstract than the bitset representation.
The pre-order and post-order integer pair as a way to represent nested heap
effects just did not click.

[pizlo]: http://www.filpizlo.com/
[pizlo-ssa]: https://gist.github.com/pizlonator/cf1e72b8600b1437dda8153ea3fdb963
[pizlo-effect]: https://gist.github.com/pizlonator/cf1e72b8600b1437dda8153ea3fdb963#uniform-effect-representation

It didn't make any sense until I actually went spelunking in JavaScriptCore and
found one of several implementations---because, you know, JSC is also six
compilers in a trenchcoat.

DFG, B3, DOMJIT, and probably others all have their own abstract heap
implementations. We'll look at DOMJIT mostly because it's a smaller example and
also illustrates something else that's interesting: builtins. We'll come back
to builtins in a minute.

Let's take a lookat how DOMJIT structures its [abstract
heaps][domjit-abstract-heaps]: a YAML file.

[domjit-abstract-heaps]: https://github.com/WebKit/WebKit/blob/989c9f9cd5b1f0c9606820e219ee51da32a34c6b/Source/WebCore/domjit/DOMJITAbstractHeapRepository.yaml

```yaml
DOM:
    Tree:
        Node:
            - Node_firstChild
            - Node_lastChild
            - Node_parentNode
            - Node_nextSibling
            - Node_previousSibling
            - Node_ownerDocument
        Document:
            - Document_documentElement
            - Document_body
```

It's a hierarchy. `Node_firstChild` is a subheap of `Node` is a subheap of...
and so on. A write to any `Node_nextSibling` is a write to `Node` is a write to
... Sibling heaps are unrelated: `Node_firstChild` and `Node_lastChild`, for
example, are disjoint.

To get a feel for this, I wired up a [simplified version][zjit-bitset] of
ZJIT's bitset generator (for *types!*) to read a YAML document and generate a
bitset. It generated the following Rust code:

[zjit-bitset]: {{ site.repo.repository_url }}/tree/{{ site.repo.branch }}/assets/code/gen_bitset.rb

[jsc-heap-range]: {{ site.repo.repository_url }}/tree/{{ site.repo.branch }}/assets/code/gen_int_pairs.rb

```rust
mod bits {
  pub const Empty: u64 = 0u64;
  pub const Document_body: u64 = 1u64 << 0;
  pub const Document_documentElement: u64 = 1u64 << 1;
  pub const Document: u64 = Document_body | Document_documentElement;
  pub const Node_firstChild: u64 = 1u64 << 2;
  pub const Node_lastChild: u64 = 1u64 << 3;
  pub const Node_nextSibling: u64 = 1u64 << 4;
  pub const Node_ownerDocument: u64 = 1u64 << 5;
  pub const Node_parentNode: u64 = 1u64 << 6;
  pub const Node_previousSibling: u64 = 1u64 << 7;
  pub const Node: u64 = Node_firstChild | Node_lastChild | Node_nextSibling | Node_ownerDocument | Node_parentNode | Node_previousSibling;
  pub const Tree: u64 = Document | Node;
  pub const DOM: u64 = Tree;
  pub const NumTypeBits: u64 = 8;
}
```

It's not a fancy X-macro, but it's a short and flexible Ruby script.

Then I took the [DOMJIT abstract heap
generator][domjit-abstract-heap-gen]---also funnily enough a short Ruby
script---modified the output format slightly, and had it generate its int
pairs:

[domjit-abstract-heap-gen]: https://github.com/WebKit/WebKit/blob/989c9f9cd5b1f0c9606820e219ee51da32a34c6b/Source/WebCore/domjit/generate-abstract-heap.rb

```rust
mod bits {
  /* DOMJIT Abstract Heap Tree.
  DOM<0,8>:
      Tree<0,8>:
          Node<0,6>:
              Node_firstChild<0,1>
              Node_lastChild<1,2>
              Node_parentNode<2,3>
              Node_nextSibling<3,4>
              Node_previousSibling<4,5>
              Node_ownerDocument<5,6>
          Document<6,8>:
              Document_documentElement<6,7>
              Document_body<7,8>
  */
  pub const DOM: HeapRange = HeapRange { start: 0, end: 8 };
  pub const Tree: HeapRange = HeapRange { start: 0, end: 8 };
  pub const Node: HeapRange = HeapRange { start: 0, end: 6 };
  pub const Node_firstChild: HeapRange = HeapRange { start: 0, end: 1 };
  pub const Node_lastChild: HeapRange = HeapRange { start: 1, end: 2 };
  pub const Node_parentNode: HeapRange = HeapRange { start: 2, end: 3 };
  pub const Node_nextSibling: HeapRange = HeapRange { start: 3, end: 4 };
  pub const Node_previousSibling: HeapRange = HeapRange { start: 4, end: 5 };
  pub const Node_ownerDocument: HeapRange = HeapRange { start: 5, end: 6 };
  pub const Document: HeapRange = HeapRange { start: 6, end: 8 };
  pub const Document_documentElement: HeapRange = HeapRange { start: 6, end: 7 };
  pub const Document_body: HeapRange = HeapRange { start: 7, end: 8 };
}
```

It already comes with a little diagram, which is super helpful for readability.

Any empty range(s) represent empty heap effects: if the start and end are the
same number, there are no effects. There is no one `Empty` value, but any empty
range could be normalized to `HeapRange { start: 0, end: 0 }`.

Maybe this was obvious to you, dear reader, but this pre-order/post-order thing
is about nested ranges! Seeing the output of the generator laid out clearly
like this made it make a lot more sense for me.

<!--
So how do we compute subtyping relationships with `HeapRange`s? We check range
overlap! Here is [DOMJIT's C++ implementation][domjit-is-subtype-of]:

[domjit-is-subtype-of]: https://github.com/WebKit/WebKit/blob/989c9f9cd5b1f0c9606820e219ee51da32a34c6b/Source/JavaScriptCore/domjit/DOMJITHeapRange.h#L99

```c++
class HeapRange {
    constexpr explicit operator bool() const {
        return m_begin != m_end;
    }

    bool isStrictSubtypeOf(const HeapRange& other) const {
        if (!*this || !other)
            return false;
        if (*this == other)
            return false;
        return other.m_begin <= m_begin && m_end <= other.m_end;
    }

    bool isSubtypeOf(const HeapRange& other) const {
        if (!*this || !other)
            return false;
        if (*this == other)
            return true;
        return isStrictSubtypeOf(other);
    }
```

This is represented by the `operator bool()`
and implicit boolean conversions. To reinforce the whole nested heap ranges
thing, `isSubtypeOf` is asking if one `HeapRange` contains another.
-->

What about checking overlap? Here is the [implementation in
JSC][jsc-range-overlap]:

[jsc-range-overlap]: https://github.com/WebKit/WebKit/blob/989c9f9cd5b1f0c9606820e219ee51da32a34c6b/Source/JavaScriptCore/domjit/DOMJITHeapRange.h#L108

```c++
namespace WTF {
// Check if two ranges overlap assuming that neither range is empty.
template<typename T>
constexpr bool nonEmptyRangesOverlap(T leftMin, T leftMax, T rightMin, T rightMax)
{
    ASSERT_UNDER_CONSTEXPR_CONTEXT(leftMin < leftMax);
    ASSERT_UNDER_CONSTEXPR_CONTEXT(rightMin < rightMax);

    return leftMax > rightMin && rightMax > leftMin;
}

// Pass ranges with the min being inclusive and the max being exclusive.
template<typename T>
constexpr bool rangesOverlap(T leftMin, T leftMax, T rightMin, T rightMax) {
    ASSERT_UNDER_CONSTEXPR_CONTEXT(leftMin <= leftMax);
    ASSERT_UNDER_CONSTEXPR_CONTEXT(rightMin <= rightMax);

    // Empty ranges interfere with nothing.
    if (leftMin == leftMax)
        return false;
    if (rightMin == rightMax)
        return false;

    return nonEmptyRangesOverlap(leftMin, leftMax, rightMin, rightMax);
}
}

class HeapRange {
    bool overlaps(const HeapRange& other) const {
        return WTF::rangesOverlap(m_begin, m_end, other.m_begin, other.m_end);
    }
}
```

(See also [How to check for overlapping intervals][overlapping-intervals] and
[Range overlap in two compares][two-compares] for more fun.)

[overlapping-intervals]: https://zayenz.se/blog/post/how-to-check-for-overlapping-intervals/
[two-compares]: https://nedbatchelder.com/blog/201310/range_overlap_in_two_compares.html

While bitsets are a dense representation (you have to hold every bit), they are
very compact and they are very precise. You can hold any number of combinations
of 64 or 128 bits in a single register. The union and intersection operations
are very cheap.

With int ranges, it's a little more complicated. An imprecise union of `a` and
`b` can take the maximal range that covers both `a` and `b`. To get a more
precise union, you have to keep track of both. In the worst case, if you want
efficient arbitrary queries, you need to store your int ranges in an interval
tree. So what gives?

I asked Fil if both bitsets and int ranges answer the same question, why use
int ranges? He said that it's more flexible long-term: bitsets get expensive as
soon as you need over 128 bits (you might need to heap allocate them!) whereas
ranges have no such ceiling. But doesn't holding sequences of ranges require
heap allocation? Well, despite Fil writing this in his SSA post:

> The purpose of the effect representation baked into the IR is to provide a
> precise always-available baseline for alias information that is super easy to
> work with. [...] you can have instructions report that they read/write
> multiple heaps [...] you can have a utility function that produces such lists
> on demand.

It's important to note that this doesn't actually involve any allocation of
lists. JSC does this very clever thing where they have "functors" that they
pass in as arguments that compress/summarize what they want to out of an
instruction's effects.

Let's take a look at how the DFG (for example) uses these heap ranges in
analysis. The DFG is structured in such a way that it can make use of the
DOMJIT heap ranges directly, which is neat.

Note that `AbstractHeap` in the example below is a thin wrapper over the DFG
compiler's own `DOMJIT::HeapRange` equivalent:

```c++
class AbstractHeapOverlaps {
public:
    AbstractHeapOverlaps(AbstractHeap heap)
        : m_heap(heap)
        , m_result(false)
    {
    }

    void operator()(AbstractHeap otherHeap) const
    {
        if (m_result)
            return;
        m_result = m_heap.overlaps(otherHeap);
    }

    bool result() const { return m_result; }

private:
    AbstractHeap m_heap;
    mutable bool m_result;
};

bool writesOverlap(Graph& graph, Node* node, AbstractHeap heap)
{
    NoOpClobberize noOp;
    AbstractHeapOverlaps addWrite(heap);
    clobberize(graph, node, noOp, addWrite, noOp);
    return addWrite.result();
}
```

`clobberize` is the function that calls these functors (`noOp` or `addWrite` in
this case) for each effect that the given IR instruction `node` declares.

I've pulled some relevant snippets of `clobberize`, which is quite long, that I
think are interesting.

First, some instructions (constants, here) have no effects. There's some
utility in the `def(PureValue(...))` call but I didn't understand fully.

Then there are some instructions that conditionally have effects depending on
the use types of their operands.[^dfg-use-type] Taking the absolute value of an
Int32 or a Double is effect-free but otherwise looks like it can run arbitrary
code.

[^dfg-use-type]: This is because the DFG compiler does this interesting thing
    where they track and guard the input types on *use* vs having types
    attached to the input's own *def*. It might be a clean way to handle shapes
    inside the type system while also allowing the type+shape of an object to
    change over time (which it can do in many dynamic language runtimes).

Some run-time IR guards that might cause side exits are annotated as
such---they write to the `SideState` heap.

Local variable instructions read *specific* heaps indexed by what looks like
the local index but I'm not sure. This means accessing two different locals
won't alias!

Instructions that allocate can't be re-ordered, it looks like; they both read
and write the `HeapObjectCount`. This probably limits the amount of allocation
sinking that can be done.

Then there's `CallDOM`, which is the builtins stuff I was talking about. We'll
come back to that after the code block.

```c++
template<typename ReadFunctor, typename WriteFunctor, typename DefFunctor, typename ClobberTopFunctor>
void clobberize(Graph& graph, Node* node, const ReadFunctor& read, const WriteFunctor& write, const DefFunctor& def)
{
    // ...

    switch (node->op()) {
    case JSConstant:
    case DoubleConstant:
    case Int52Constant:
        def(PureValue(node, node->constant()));
        return;

    case ArithAbs:
        if (node->child1().useKind() == Int32Use || node->child1().useKind() == DoubleRepUse)
            def(PureValue(node, node->arithMode()));
        else
            clobberTop();
        return;

    case AssertInBounds:
    case AssertNotEmpty:
        write(SideState);
        return;

    case GetLocal:
        read(AbstractHeap(Stack, node->operand()));
        def(HeapLocation(StackLoc, AbstractHeap(Stack, node->operand())), LazyNode(node));
        return;

    case NewArrayWithSize:
    case NewArrayWithSizeAndStructure:
        read(HeapObjectCount);
        write(HeapObjectCount);
        return;

    case CallDOM: {
        const DOMJIT::Signature* signature = node->signature();
        DOMJIT::Effect effect = signature->effect;
        if (effect.reads) {
            if (effect.reads == DOMJIT::HeapRange::top())
                read(World);
            else
                read(AbstractHeap(DOMState, effect.reads.rawRepresentation()));
        }
        if (effect.writes) {
            if (effect.writes == DOMJIT::HeapRange::top()) {
                if (Options::validateDFGClobberize())
                    clobberTopFunctor();
                write(Heap);
            } else
                write(AbstractHeap(DOMState, effect.writes.rawRepresentation()));
        }
        ASSERT_WITH_MESSAGE(effect.def == DOMJIT::HeapRange::top(), "Currently, we do not accept any def for CallDOM.");
        return;
    }
    }
}
```

(Remember that these `AbstractHeap` operations are very similar to DOMJIT's
`HeapRange` with a couple more details---and in some cases even contain DOMJIT
`HeapRange`s!)

This `CallDOM` node is the way for the DOM APIs in the browser---a significant
chunk of the builtins, which are written in C++---to communicate what they do
to the optimizing compiler. Without any annotations, the JIT has to assume that
a call into C++ could do anything to the JIT state. Bummer!

But because, for example, [`Node.firstChild`][node-firstchild] [annotates what
memory it reads from][firstchild-annotation] and what it *doesn't* write to,
the JIT can optimize around it better---or even remove the access completely.
It means the JIT can reason about calls to known builtins *the same way* that
it reasons about normal JIT opcodes.

(Incidentally it looks like it doesn't even make a C call, but instead is
inlined as a little memory read snippet using a JIT builder API. Neat.)

[node-firstchild]: https://developer.mozilla.org/en-US/docs/Web/API/Node/firstChild
[firstchild-annotation]: https://github.com/WebKit/WebKit/blob/32bda1b1d73527ba1d05ccba0aa8e463ddeac56d/Source/WebCore/domjit/JSNodeDOMJIT.cpp#L86

<!-- TODO tie it back to the original example -->

<!--
B3 from JSC
https://github.com/WebKit/WebKit/blob/main/Source/JavaScriptCore/b3/B3Effects.h
https://github.com/WebKit/WebKit/blob/5811a5ad27100acab51f1d5ba4518eed86bbf00b/Source/JavaScriptCore/b3/B3AbstractHeapRepository.h

DOMJIT from JSC
https://github.com/WebKit/WebKit/blob/main/Source/WebCore/domjit/generate-abstract-heap.rb
generates from https://github.com/WebKit/WebKit/blob/b99cb96a7a3e5978b475d2365b72196e15a1a326/Source/WebCore/domjit/DOMJITAbstractHeapRepository.yaml#L4

DFG from JSC
https://github.com/WebKit/WebKit/blob/b99cb96a7a3e5978b475d2365b72196e15a1a326/Source/JavaScriptCore/dfg/DFGAbstractHeap.h
https://github.com/WebKit/WebKit/blob/b99cb96a7a3e5978b475d2365b72196e15a1a326/Source/JavaScriptCore/dfg/DFGClobberize.h
https://github.com/WebKit/WebKit/blob/b99cb96a7a3e5978b475d2365b72196e15a1a326/Source/JavaScriptCore/dfg/DFGClobberize.cpp
https://github.com/WebKit/WebKit/blob/b99cb96a7a3e5978b475d2365b72196e15a1a326/Source/JavaScriptCore/dfg/DFGClobberize.h
https://github.com/WebKit/WebKit/blob/b99cb96a7a3e5978b475d2365b72196e15a1a326/Source/JavaScriptCore/dfg/DFGStructureAbstractValue.cpp
https://github.com/WebKit/WebKit/blob/b99cb96a7a3e5978b475d2365b72196e15a1a326/Source/JavaScriptCore/dfg/DFGStructureAbstractValue.h
https://github.com/WebKit/WebKit/blob/b99cb96a7a3e5978b475d2365b72196e15a1a326/Source/JavaScriptCore/dfg/DFGClobberSet.h
https://github.com/WebKit/WebKit/blob/b99cb96a7a3e5978b475d2365b72196e15a1a326/Source/JavaScriptCore/dfg/DFGStructureAbstractValue.h
-->

Last, we'll look at Simple, which has a slightly different take on all of this.

## Simple

[Simple](https://github.com/seaofnodes/simple) is Cliff Click's pet Sea of
Nodes (SoN) project to try and showcase the idea to the world---outside of a
HotSpot C2 context.

This one is a little harder for me to understand but it looks like each
translation unit has a [`StartNode`][simple-startnode-java] that doles out
different classes of memory nodes for each alias class. Each IR node then takes
data dependencies on whatever effect nodes it might uses.

[simple-startnode-java]: https://github.com/SeaOfNodes/Simple/blob/1426384fc7d0e9947e38ad6d523a5e53c324d710/chapter10/src/main/java/com/seaofnodes/simple/node/StartNode.java#L33

Alias classes are split up based on the paper [Type-Based Alias Analysis][tbaa]
(PDF): "Our approach is a form of TBAA similar to the 'FieldTypeDecl' algorithm
described in the paper."

[tbaa]: /assets/img/tbaa.pdf

<!-- TODO: insert Cliff Click messages -->

The Simple project is structured into sequential implementation stages and
alias classes come into the picture in [Chapter 10][simple-chapter-10].

[simple-chapter-10]: https://github.com/SeaOfNodes/Simple/tree/main/chapter10

Because I spent a while spelunking through other implementations to see how
other projects did this, here is a list of the projects I looked at. Mostly,
they use bitsets.

## Other implementations

### HHVM

[HHVM](https://github.com/facebook/hhvm), a JIT for the
[Hack](https://hacklang.org/) language, also uses a bitset for its memory
effects. See for example: [alias-class.h][hhvm-alias-class-h] and
[memory-effects.h][hhvm-memory-effects-h].

[hhvm-alias-class-h]: https://github.com/facebook/hhvm/blob/0395507623c2c08afc1d54c0c2e72bc8a3bd87f1/hphp/runtime/vm/jit/alias-class.h
[hhvm-memory-effects-h]: https://github.com/facebook/hhvm/blob/0395507623c2c08afc1d54c0c2e72bc8a3bd87f1/hphp/runtime/vm/jit/memory-effects.h

HHVM has a couple places that use this information, such as [a
definition-sinking pass][hhvm-def-sink-cpp], [alias
analysis][hhvm-alias-analysis-h], [DCE][hhvm-dce-cpp], [store
elimination][hhvm-store-elim-cpp], and more.

[hhvm-def-sink-cpp]: https://github.com/facebook/hhvm/blob/4cdb85bf737450bf6cb837d3167718993f9170d7/hphp/runtime/vm/jit/def-sink.cpp
[hhvm-alias-analysis-h]: https://github.com/facebook/hhvm/blob/0395507623c2c08afc1d54c0c2e72bc8a3bd87f1/hphp/runtime/vm/jit/alias-analysis.h
[hhvm-dce-cpp]: https://github.com/facebook/hhvm/blob/4cdb85bf737450bf6cb837d3167718993f9170d7/hphp/runtime/vm/jit/dce.cpp
[hhvm-store-elim-cpp]: https://github.com/facebook/hhvm/blob/4cdb85bf737450bf6cb837d3167718993f9170d7/hphp/runtime/vm/jit/store-elim.cpp

If you are wondering why the HHVM representation looks similar to the Cinder
representation, it's because some former HHVM engineers such as Brett Simmers
also worked on Cinder!

### Android ART

(note that I am linking an ART fork on GitHub as a reference, but the upstream
code is [hosted on googlesource][googlesource-art])

[googlesource-art]: https://android.googlesource.com/platform/art/+/refs/heads/main/compiler/optimizing/nodes.h

Android's [ART Java runtime](https://source.android.com/docs/core/runtime) also
uses a bitset for its effect representation. It's a very compact class called
`SideEffects` in [nodes.h][art-nodes-h].

[art-nodes-h]: https://github.com/LineageOS/android_art/blob/c09a5c724799afdc5f89071b682b181c0bd23099/compiler/optimizing/nodes.h#L1602

The side effects are used in [loop-invariant code motion][art-licm-cc], [global
value numbering][art-gvn-cc], [write barrier
elimination][art-write-barrier-elimination-cc], [scheduling][art-scheduler-cc],
and more.

[art-licm-cc]: https://github.com/LineageOS/android_art/blob/c09a5c724799afdc5f89071b682b181c0bd23099/compiler/optimizing/licm.cc#L104
[art-gvn-cc]: https://github.com/LineageOS/android_art/blob/c09a5c724799afdc5f89071b682b181c0bd23099/compiler/optimizing/gvn.cc#L204
[art-write-barrier-elimination-cc]: https://github.com/LineageOS/android_art/blob/c09a5c724799afdc5f89071b682b181c0bd23099/compiler/optimizing/write_barrier_elimination.cc#L45
[art-scheduler-cc]: https://github.com/LineageOS/android_art/blob/c09a5c724799afdc5f89071b682b181c0bd23099/compiler/optimizing/scheduler.cc#L55

### .NET/CoreCLR

CoreCLR mostly [uses a bitset][clr-sideeffects-h] for its `SideEffectSet`
class. This one is interesting though because it also splits out effects
specifically to include sets of local variables (`LclVarSet`).

[clr-sideeffects-h]: https://github.com/dotnet/runtime/blob/a0878687d02b42034f4ea433ddd7a72b741510b8/src/coreclr/jit/sideeffects.h#L169

### V8

V8 is about six completely different compilers in a trenchcoat.{{ site.citation_needed }}

Turboshaft uses a struct in [operations.h][turboshaft-operations-h] called
`OpEffects` which is two bitsets for reads/writes of effects. This is used in
[value numbering][turboshaft-value-numbering-reducer-h] as well a bunch of
other small optimization passes they call "reducers".

[turboshaft-operations-h]: https://github.com/v8/v8/blob/e817fdf31a2947b2105bd665067d92282e4b4d59/src/compiler/turboshaft/operations.h#L577
[turboshaft-value-numbering-reducer-h]: https://github.com/v8/v8/blob/42f5ff65d12f0ef9294fa7d3875feba938a81904/src/compiler/turboshaft/value-numbering-reducer.h#L164

Maglev also has this thing called `NodeT::kProperties` in [their IR
nodes][maglev-ir-h] that also looks like a bitset and is used in their various
reducers. It has effect query methods on it such as `can_eager_deopt` and
`can_write`.

[maglev-ir-h]: https://github.com/v8/v8/blob/42f5ff65d12f0ef9294fa7d3875feba938a81904/src/maglev/maglev-ir.h

Until recently, V8 also used Sea of Nodes as its IR representation, which also
tracks side effects more explicitly in the structure of the IR itself.

## Guile

[Guile Scheme][guile] looks like it has a [custom tagging
scheme][guile-effects] type thing.

[guile]: https://www.gnu.org/software/guile/
[guile-effects]: https://wingolog.org/archives/2014/05/18/effects-analysis-in-guile

## Conclusion

Both bitsets and int ranges are perfectly cromulent ways of representing heap
effects for your IR. The Sea of Nodes approach is also probably okay since it
powers HotSpot C2 and (for a time) V8.

Remember to ask *the right questions* of your IR when doing analysis.

## Thank you

Thank you to [Fil Pizlo](http://www.filpizlo.com/) for writing his initial
GitHub Gist and sending me on this journey and thank you to [Chris
Gregory](https://www.chrisgregory.me/) and [Ufuk
Kayserilioglu](https://ufuk.dev/) for feedback on making some of the
explanations more helpful.

<!--

TODO Dart
https://github.com/dart-lang/sdk/blob/59905c43f1a0394394ad5545ee439bcba63dea55/runtime/vm/constants_riscv.h#L968
https://github.com/dart-lang/sdk/blob/59905c43f1a0394394ad5545ee439bcba63dea55/runtime/vm/compiler/backend/redundancy_elimination.cc#L758
https://github.com/dart-lang/sdk/blob/59905c43f1a0394394ad5545ee439bcba63dea55/runtime/vm/compiler/backend/redundancy_elimination.cc#L1096

ChakraCore
https://github.com/chakra-core/ChakraCore/blob/2dba810c925eb366e44a1f7d7a5b2e289e2f8510/lib/Runtime/Types/RecyclableObject.h#L172

SpiderMonkey
https://github.com/servo/mozjs/blob/77645ed41f588297fd8d7edaee71500f4c83d070/mozjs-sys/mozjs/js/src/jit/MIR.h#L935
https://github.com/servo/mozjs/blob/77645ed41f588297fd8d7edaee71500f4c83d070/mozjs-sys/mozjs/js/src/jit/MIR.h#L9658

Cinder LIR
https://github.com/facebookincubator/cinderx/blob/main/cinderx/Jit/lir/instruction.h

HotSpot C1

HotSpot C2

PyPy
https://github.com/pypy/pypy/blob/main/rpython/jit/codewriter/effectinfo.py
https://github.com/pypy/pypy/blob/main/rpython/jit/metainterp/optimizeopt/heap.py#L59

LLVM
https://llvm.org/docs/LangRef.html#tbaa-metadata

LLVM MemorySSA
https://llvm.org/docs/MemorySSA.html

MLIR
https://mlir.llvm.org/docs/Rationale/SideEffectsAndSpeculation/

MEMOIR
https://conf.researchr.org/details/cgo-2024/cgo-2024-main-conference/31/Representing-Data-Collections-in-an-SSA-Form

Scala LMS graph IR
https://2023.splashcon.org/details/splash-2023-oopsla/46/Graph-IRs-for-Impure-Higher-Order-Languages-Making-Aggressive-Optimizations-Affordab

MIR and borrow checker
https://rustc-dev-guide.rust-lang.org/part-3-intro.html#source-code-representation

> "Fabrice Rastello, Florent Bouchez Tichadou (2022) SSA-based Compiler Design"--most (all?) chapters in Part III, Extensions, are pretty much motivated by doing alias analysis in some way

Intermediate Representations in Imperative Compilers: A Survey
http://kameken.clique.jp/Lectures/Lectures2013/Compiler2013/a26-stanier.pdf

Partitioned Lattice per Variable (PLV) -- that's in Chapter 13 on SSI

TODO maybe lattice in ascent

-->

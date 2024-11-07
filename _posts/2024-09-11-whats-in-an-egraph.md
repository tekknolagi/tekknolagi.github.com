---
title: "What's in an e-graph?"
layout: post
date: 2024-09-11
---

*This post follows from several conversations with [CF
Bolz-Tereick](https://cfbolz.de/), [Philip
Zucker](https://www.philipzucker.com/), [Chris Fallin](https://cfallin.org/),
and [Max Willsey](https://www.mwillsey.com/).*

Compilers are all about program representations. They take in a program in one
language, transform some number of ways through some different internal
languages, and output the program in another language[^languages].

[^languages]: This is not to say that the languages have to be distinct; the
    compiler can, say, take in a program in C and emit a program in C. And
    sometimes the term "language" gets fuzzy, since (for example) C23 is
    *technically* a different language than C99 but they are both recognizable
    as C. But there's value in a C23 to C99 compiler because not all compilers
    can take in C23 input in the front-end yet. And also sometimes the term
    compiler comes with an implication that the input language is in some way
    "higher level" than the output language, and this vibe ended up producing
    the term "transpiler", but eh. Compiler take program in and compiler emit
    program out.

Part of the value in the inter-language transformations is optimizing the
program. This can mean making it faster, smaller, or something else. Optimizing
requires making changes to the program. For example, consider the following
piece of code in a made-up IR:

```
v0 = ...
v1 = Const 8
v2 = v0 * v1
```

If the compiler wants to optimize the instruction for `v2`, it has to go
through and logically replace all uses of `v2` with the replacement
instruction. This is a rewrite.

Many compilers will go through and iterate through every instruction and check
if that instruction `op` uses the original instruction `v2`. If it does, it
will swap all of its uses of `v2` with the replacement instruction.

```c++
void very_specific_optimization(Instr* instr) {
  if (instr->IsMul() && instr->Operand(1)->IsConst() &&
      instr->Operand(1)->AsConst()->Value() == 8) {
    Instr* replacement = new LeftShift(instr->Operand(0), new Const(3));
    for (auto op : block.ops) {
      if (op->uses(instr)) {
        op->replace_use(instr, replacement);
      }
    }
  }
}
```

This is fine. It's very traditional. Depending on the size and complexity of
your programs, this can work. It's how the [Cinder
JIT](https://github.com/facebookincubator/cinder/) works for its two IRs. It's
very far from causing any performance problems in the compiler. But there are
other compilers with other constraints and therefore other approaches to doing
these rewrites.

In this post, I'm going to take you on a bit of a meandering walk. We'll start
from an alternative to find-and-replace called *union-find* and then
incrementally add features until we accidentally have built another data
structure called an *e-graph*. Hopefully it removes some of the mystery.

## Union-find

I love union-find. It enables fast, easy, in-place IR rewrites for compiler
authors. Its API has two main functions: `union` and `find`. The minimal
implementation is about 15 lines of code and is embeddable directly in your IR.

Instead of iterating through every operation in the basic block and swapping
pointers, we instead mark our IR node as "pointing to" another node. The below
snippet replaces the entire loop in the previous example:

```c++
instr->make_equal_to(new LeftShift(instr->Operand(0), new Const(3)));
```

This notion of a forwarding pointer can be either embedded in the IR node
itself or in an auxiliary table. Each node maintains its source of truth, and
each rewrite takes only one pointer swap (yes, there's some pointer chasing,
but it's *very little* pointer chasing[^advanced-features]). It's a classic
time-space trade-off, though. You have to store ~1 additional pointer of space
for each IR node.

[^advanced-features]: The naive implementations shown in this post are not the
    optimal ones that everyone oohs and ahhs about. Those have things like
    path compression. The nice thing is that the path compression is an
    add-on feature that doesn't change the API at all. Then if you get
    hamstrung by the inverse Ackermann function, you have other problems with
    the size of your IR graph.

See below an adaptation of CF's implementation from the toy optimizer
series[^also-phil]:

[^also-phil]: See also this tidy little union-find implementation by Phil from
    [his blog post](https://www.philipzucker.com/compile_constraints/):

    ```python
    uf = {}
    def find(x):
      while x in uf:
        x = uf[x]
      return x

    def union(x,y):
      x = find(x)
      y = find(y)
      if x != y:
        uf[x] = y
      return y
    ```

    I really enjoy that it reads like a margin note. The only downside, IMO,
    is that it requires the IR operations to be both hashable and comparable
    (`__hash__` and `__eq__`).

```python
from __future__ import annotations
from dataclasses import dataclass
from typing import Optional

@dataclass
class Expr:
    forwarded: Optional[Expr] = dataclasses.field(
        default=None,
        init=False,
        compare=False,
        hash=False,
    )

    def find(self) -> Expr:
        """Return the representative of the set containing `self`."""
        expr = self
        while expr is not None:
            next = expr.forwarded
            if next is None:
                return expr
            expr = next
        return expr

    def make_equal_to(self, other) -> None:
        """Union the set containing `self` with the set containing `other`."""
        found = self.find()
        if found is not other:
            found.forwarded = other
```

Union-find can be so fast because it is limited in its expressiveness:

* There's no built-in way to enumerate the elements of a set
* Each set has a single representative element
* We only care about the representative of a set

(If you want to read more about it, check out the first half of my other post,
[Vectorizing ML models for fun](/blog/vectorizing-ml-models/), the [toy
optimizer](https://pypy.org/posts/2022/07/toy-optimizer.html), [allocation
removal in the toy
optimizer](https://pypy.org/posts/2022/10/toy-optimizer-allocation-removal.html),
and [abstract interpretation in the toy
optimizer](https://bernsteinbear.com/blog/toy-abstract-interpretation/).)

This is really great for some compiler optimizations, like the strength
reduction we did above. Here is the IR snippet again:

```
v0 = ...
v1 = Const 8
v2 = v0 * v1
```

A strength reduction pass might rewrite `v2` as a left shift instead of a
multiplication (`v2.make_equal_to(LeftShift(v0, Const(3)))`) because left
shifts are often faster than multiplications. That's great; we got a small
speedup.

But seemingly obvious local rewrites can have less-local consequences. Consider
the expression `(a * 2) / 2`, which is the example from the [e-graphs
good](https://egraphs-good.github.io/) website and paper. If our strength
reduction pass eagerly rewrites `a * 2` to `a << 1`, we've lost some
information.

This rewrite stops another hypothetical pass from recognizing that expressions
of the form `(a * b) / b` are equivalent to `a * (b / b)` and therefore
equivalent to `a`. This is because rewrites that use union-find are
destructive; we've gotten rid of the multiplication. How might we find it
again?

## Enumerating the equivalence classes

Let's make this more concrete and conjure a little math IR. We'll base it on
the `Expr` base class because it's rewritable using union-find.

```python
@dataclass
class Const(Expr):
    value: int

@dataclass
class Var(Expr):
    name: str

@dataclass
class BinaryOp(Expr):
    left: Expr
    right: Expr

@dataclass
class Add(BinaryOp):
    pass

@dataclass
class Mul(BinaryOp):
    pass

@dataclass
class Div(BinaryOp):
    pass

@dataclass
class LeftShift(BinaryOp):
    pass
```

It's just constants and variables and binary operations but it'll do for our
demo.

Let's also write a little optimization pass that can do limited constant
folding, simplification, and strength reduction. We have a function
`optimize_one` that looks at an individual operation and tries to simplify it
and a function `optimize` that applies `optimize_one` to a list of
operations---a basic block, if you will.

```python
def is_const(op: Expr, value: int) -> bool:
    return isinstance(op, Const) and op.value == value

def optimize_one(op: Expr) -> None:
    if isinstance(op, BinaryOp):
        left = op.left.find()
        right = op.right.find()
        if isinstance(op, Add):
            if isinstance(left, Const) and isinstance(right, Const):
                op.make_equal_to(Const(left.value + right.value))
            elif is_const(left, 0):
                op.make_equal_to(right)
            elif is_const(right, 0):
                op.make_equal_to(left)
        elif isinstance(op, Mul):
            if is_const(left, 1):
                op.make_equal_to(right)
            elif is_const(right, 1):
                op.make_equal_to(left)
            elif is_const(right, 2):
                op.make_equal_to(Add(left, left))
                op.make_equal_to(LeftShift(left, Const(1)))

def optimize(ops: list[Expr]):
    for op in ops:
        optimize_one(op.find())
```

Let's give it a go and see what it does to our initial smaller IR snippet that
added two constants[^printing-niceties]:

[^printing-niceties]: I sneakily added some printing niceties to the `Expr`
    class that I didn't show here. They're not important for the point I'm
    making and appear in the full code listing.

```python
ops = [
    a := Const(1),
    b := Const(2),
    c := Add(a, b),
]
print("BEFORE:")
for op in ops:
    print(f"v{op.id} =", op.find())
optimize(ops)
print("AFTER:")
for op in ops:
    print(f"v{op.id} =", op.find())
# BEFORE:
# v0 = Const<1>
# v1 = Const<2>
# v2 = Add v0 v1
# AFTER:
# v0 = Const<1>
# v1 = Const<2>
# v2 = Const<3>
```

Alright, it works. We can fold `1+2` to `3`. Hurrah. But the point of this
section of the post is to discover the equivalence classes implicitly
constructed by the union-find structure. Let's write a function to do that.

To build such a function, we'll need to iterate over all operations created. I
chose to explicitly keep track of every operation in a list, but you could also
write a function to walk the `forwarded` chains of all reachable operations.

```python
every_op = []

@dataclass
class Expr:
    # ...
    def __post_init__(self) -> None:
        every_op.append(self)

# ...

def discover_eclasses(ops: list[Expr]) -> dict[Expr, set[Expr]]:
    eclasses: dict[Expr, set[Expr]] = {}
    for op in ops:
        found = op.find()
        if found not in eclasses:
            # Key by the representative
            eclasses[found] = set()
        eclasses[found].add(op)
        if op is not found:
            # Alias the entries so that looking up non-representatives also
            # finds equivalent operations
            eclasses[op] = eclasses[found]
    return eclasses

# ...
print("ECLASSES:")
eclasses = discover_eclasses(every_op.copy())
for op in ops:
    print(f"v{op.id} =", eclasses[op])
# BEFORE:
# v0 = Const<1>
# v1 = Const<2>
# v2 = Add v0 v1
# AFTER:
# v0 = Const<1>
# v1 = Const<2>
# v2 = Const<3>
# ECLASSES:
# v0 = {Const<1>}
# v1 = {Const<2>}
# v2 = {Const<3>, Add v0 v1}
```

Let's go back to our more complicated IR example from the egg website, this
time expressed in our little IR:

```python
ops = [
    a := Var("a"),
    b := Const(2),
    c := Mul(a, b),
    d := Div(c, b),
]
```

If we run our optimizer on it right now, we'll eagerly rewrite the
multiplication into a left-shift, but then rediscover the multiply in the
equivalence classes (now I've added little `*` to indicate the union-find
representatives of each equivalence class):

```
BEFORE:
v0 = Var<a>
v1 = Const<2>
v2 = Mul v0 v1
v3 = Div v2 v1
AFTER:
v0 = Var<a>
v1 = Const<2>
v2 = LeftShift v0 v5
v3 = Div v6 v1
ECLASSES:
v0 = * {Var<a>}
v1 = * {Const<2>}
v2 =   {LeftShift v0 v5, Add v0 v0, Mul v0 v1}
v3 = * {Div v6 v1}
v4 =   {LeftShift v0 v5, Add v0 v0, Mul v0 v1}
v5 = * {Const<1>}
v6 = * {LeftShift v0 v5, Add v0 v0, Mul v0 v1}
```

That solves one problem: at any point, we can enumerate the equivalence classes
stored in the union-find structure. But, like all data structures, the
union-find representation we've chosen has a trade-off: fast to rewrite, slow
to enumerate. We'll accept that for now.

<!-- TODO parallel worlds of graphs -->

This enumeration feature on its own does not comprise one of the APIs of an
e-graph. To graft on e-matching to union-find, we'll need to do one more step:
a search. Some would call it `match`.

## Matching

So we can rediscover the multiplication even after reducing it to a left shift.
That's nice. But how can we do pattern matching on this data representation?

Let's return to `(a * b) / b`. This corresponds to the IR-land Python
expression of `Div(Mul(a, b), b)` for any expressions `a` and `b` (and keeping
the `b`s equal, which is not the default in a Python `match` pattern).

For a given operation, we can see if there is a `Div` in its equivalence class
by looping over the entire equivalence class:

```python
def optimize_match(op: Expr, eclasses: dict[Expr, set[Expr]]):
    # Find cases of the form a / b
    for e0 in eclasses[op]:
        if isinstance(e0, Div):
            # ...
```

That's all well and good, but how do we find if it's a `Div` of a `Mul`? We
loop again!

```python
def optimize_match(op: Expr, eclasses: dict[Expr, set[Expr]]):
    # Find cases of the form (a * b) / c
    for e0 in eclasses[op]:
        if isinstance(e0, Div):
            div_left = e0.left
            div_right = e0.right
            for e1 in eclasses[div_left]:
                if isinstance(e1, Mul):
                    # ...
```

Note how we don't need to call `.find()` on anything because we've already
aliased the set in the equivalence classes dictionary for convenience.

And how do we hold the `b`s equal? Well, we can check if they match:

```python
def optimize_match(op: Expr, eclasses: dict[Expr, set[Expr]]):
    # Find cases of the form (a * b) / b
    for e0 in eclasses[op]:
        if isinstance(e0, Div):
            div_left = e0.left
            div_right = e0.right
            for e1 in eclasses[div_left]:
                if isinstance(e1, Mul):
                    mul_left = e1.left
                    mul_right = e1.right
                    if mul_right == div_right:
                        # ...
```

And then we can rewrite the `Div` to the `Mul`'s left child:

```python
def optimize_match(op: Expr, eclasses: dict[Expr, set[Expr]]):
    # Find cases of the form (a * b) / b and rewrite to a
    for e0 in eclasses[op]:
        if isinstance(e0, Div):
            div_left = e0.left
            div_right = e0.right
            for e1 in eclasses[div_left]:
                if isinstance(e1, Mul):
                    mul_left = e1.left
                    mul_right = e1.right
                    if mul_right == div_right:
                        op.make_equal_to(mul_left)
                        return
```

If we run this optimization function for every node in our basic block, we end
up with:

```
AFTER:
v0 = Var<a>
v1 = Const<2>
v2 = LeftShift v0 v5
v3 = Var<a>
```

where `v3` corresponds to our original big expression. Congratulations, you've
successfully implemented a time-traveling compiler pass!

Unfortunately, it's very specific: our match conditions are hard-coded into the
loop structure and the loop structure (how many levels of nesting) is
hard-coded into the function. This is the sort of thing that our programming
giants invented SQL to solve[^egg-relational].

[^egg-relational]: This is a bit of a head-nod to the good folks working on egg
    and egglog, a team comprised of compilers people and database people. They
    have realized that the e-graph and the relational database are very similar
    and are building tools that do a neat domain crossover. Read [the
    paper](https://dl.acm.org/doi/10.1145/3591239) (open access) if you are
    interested in learning more!

    Yihong Zhang has implemented
    [egraph-sqlite](https://github.com/yihozhang/egraph-sqlite), which is
    delightfully small, in Racket. I would love to see it ported to other
    langauges for fun and learning!

We don't have time or brainpower to implement a full query
language[^nerd-snipe] and I ran out of ideas for making a small embedded
matching DSL, so you will have to take my word for it that it's tractable.

<!-- TODO: a matching DSL

, so in this post will implement a small pattern-matching
DSL that kind of vaguely maybe-if-you-squint looks like something
relational. -->

[^nerd-snipe]: As soon as I wrote this I thought "how hard could it be?" and
    went off to learn more and find the smallest SQL-like implementation. I
    eventually found [SQLToy](https://github.com/weinberg/SQLToy) (~500LOC JS)
    and [ported it to Python](https://github.com/tekknolagi/db.py/) (~200 LOC).
    I don't know that having this embedded in the post or a minimal e-graph
    library would help, exactly, but it was a fun learning experience.

One thing to note: after every write with `make_equal_to`, we need to
rediscover the eclasses if we want to read from them again. I think this is
what the egg people call a "rebuild" and part of what made their paper
interesting was finding a way to do this less often or faster.

Another thing we need to do, I think, is iterate until convergence. It's not
guaranteed that we will always reach the so-called "congruence closure" with
one pass over all of the operations, matching and rewriting. In some cases
(which?), the graph may not even converge at all!

Now what we have is a bunch of parallel worlds for our basic block where each
operation is actually a set of equivalent operations. But which element of the
set should we pick? One approach, the one we were taking before, is to just
pick the representative as the desired final form of each operation. This is a
very union-find style approach. It's straightforward, it's fast, and it works
well in a situation where we only ever do strength reduction type rewrites.

But e-graphs popped into the world because people wanted to explore a bigger
state space. It's possible that the representative of an equivalence class is
locally optimal but not globally optimal. If our optimality function---our cost
function---considers the entire program, we have to find a way to broaden our
search.

## Extracting

The final piece of the e-graph API is an `extract` function. This function
finds the "lowest cost" or "most optimal" version of the program in the
e-graph.

The simplest extraction function is to iterate through the cartesian product of
all of the equivalence classes for each IR node and find the one that minimizes
the whole-program cost.

```python
import itertools
def extract(program: list[Expr], eclasses: dict[Expr, set[Expr]]) -> list[Expr]:
    best_cost = float("inf")
    best_program = program
    for trial_program in itertools.product(*[eclasses[op] for op in program]):
        cost = whole_program_cost(trial_program)
        if cost < best_cost:
            best_cost = cost
            best_program = trial_program.copy()
    return best_program
```

Unfortunately, this is slow. As the number of nodes grows, your base grows and
as the number of rewrites grows, your exponent grows. It's bad news bears.
There are a bunch of different approaches that don't involve exhaustive search,
but they do not always produce the globally optimal program. It's an active
area of research.

Another thing to note is that the cost function isn't normally built-in to
e-graph implementations; usually they allow library users to provide at least
their own cost functions, if not the entirety of *extract*.

## Wrapping up

This brings us to a complete e-graph implementation. We started with a simple
union-find, and by incrementally adding `match` and `extract`, we ended with a
full e-graph.

A bit of a subtle note here is that unlike our more procedural/functional
pattern matching situation with a manual `make_equal_to`, many (most?) e-graph
implementations tend to suggest that the library user provides a set of
declarative syntactic rewrite rules.
[egglog](https://github.com/egraphs-good/egglog) uses a custom Datalog-like
DSL;
[Cranelift](https://cranelift.dev/)
uses a DSL called ISLE; [Ego](https://ocaml.org/p/ego/0.0.6/doc/index.html)
uses an embedded DSL.

```lisp
;; This is a Datalog-like pair of rules from an egglog example
(rewrite (If T t f) t)
(rewrite (If F t f) f)
```

This requires embedding your compiler's notion of the program into the
library's domain, then extracting back out from the library's domain into your
own IR.

Part of the motivation for this blog post was to provide a kind of e-graph that
can be embedded directly into your compiler project without mapping back and
forth.

## Further reading

Cranelift uses a modified form of e-graph called an [aegraph][aegraph]. It's
different in that the entire e-graph can be topo sorted and equality arrows can
only point to earlier nodes. There are probably some very interesting
trade-offs here but I am not an expert and you should probably read Chris
Fallin's [excellent post][aegraph].

[aegraph]: https://github.com/bytecodealliance/rfcs/blob/main/accepted/cranelift-egraph.md

In a [Zulip thread][zulip], Chris writes:

> aegraphs are really about three key things:
>
> - a persistent immutable data structure that encodes eclasses directly -- via the use of "union nodes" that refer to two other nodes. This lets us refer to a "snapshot in time" of an eclass, before we union more things into it, which turns out to be important for acyclicity
> - a rewrite strategy that is eager ("bottom-up"): as soon as we create a node, we apply rewrite rules just to that node. That was actually kind of my entrypoint to doing something "different" than egg: I was wondering how to apply rules in a more efficient way than "iterate over all nodes and apply all rules" and, well, doing rewrites just once is about as good as one can do
> - acyclicity: in a classical egraph one can get cycles when merging nodes even when no cycles exist in the input. Consider `x + 0`, and a rule that rewrites that to `x`. Then we have one eclass that refers to itself -- in essence it's encoding the equivalence in *both* directions, so it could also be `(((x + 0) + 0) + 0)` or longer to infinity; that's what the cycle denotes. To avoid that we have to refer to a *snapshot* of the eclass as we know it in the args, and never re-intern a value node with new (union'd) arg eclasses. That enables the persistent immutable data structure; and requires the eager rewrite to work.
>
> (in my [talk about this](https://cfallin.org/pubs/egraphs2023_aegraphs_slides.pdf) I have this wonky three-sided figure where each of these concepts mutually reinforces the other...)
>
> One of the things that surprised me is that the single-pass eager rewrite *does* actually work -- it works if one's rules are structured in a certain way. The case one wants to avoid is where A rewrites to B, C rewrites to B, and then a better version of A is actually C (but no direct rewrite exists) -- that's where later unification in a full egraph would have grouped A and C together and that equivalence would be visible, but eager rewrites with snapshotted eclasses does not. It turns out the way we write rules in Cranelift at least is "directional" enough that we don't have this in practice (it would require C to be better than B, even though we have a C->B rewrite).
>
> There's a whole other side to Cranelift's use of aegraphs having to do with control flow, "elaboration", the way we do GVN (without partial redundancy) and LICM, keep side effects in the right place, and getting the reconstructed/reserialized sequence of computations correct with respect to dominance (extraction needs to worry about the domtree!).

[zulip]: https://egraphs.zulipchat.com/#narrow/stream/375765-egg.2Fegglog/topic/incrementally.20.22discovering.22.20e-graphs.20from.20union-find

PyPy has "union find" in its optimizer but it's smarter than normal union-find.
It also has smart constructors and some other features that make its optimizer
more e-graph like than union-find like. Perhaps CF will write a blog post about
this some day.

And of course check out egg and egglog, the main e-graph libraries around. And
Metatheory.jl, too.

* [Efficient E-matching for SMT Solvers](http://leodemoura.github.io/files/ematching.pdf) (PDF)

Please let me know what thoughts you have! This is a very new subject for me.

Thanks to Kartik Agaram, Max Willsey, and Chris Fallin for reviewing drafts of this post.

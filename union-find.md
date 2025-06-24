---
layout: page
title: Union-find
permalink: /union-find/
---

Union-find, sometimes called disjoint-set union, is a data structure that
stores equivalence classes quickly and compactly. It has a bunch of uses:

* compiler optimizer rewriting
* [type inference](/blog/type-inference/)
* pointer analysis (see Steensgaard's algorithm)
* into-SSA ([Bebenita's algorithm](/assets/img/bebenita-ssa.pdf) (PDF))
* probably more

I think about it mostly in from a compilers perspective because that's all I do
all day, every day: rewrite those graphs.

It's *especially* interesting to me because the core of the simple
implementation is so small:

```python
# Implementation courtesy Phil Zucker
uf = {}

def find(x):  # Find the set representative
    while x in uf:
        x = uf[x]
    return x

def union(x, y):  # Join two sets
    x = find(x)
    y = find(y)
    if x is not y:
        uf[x] = y
```

This makes it so easy to drop this into any existing or new project. No
library, just 10 lines of code. Instant find-and-replace when optimizing your
compiler IR.

This implementation does not do path compression or union-by-rank, the features
that get peak performance, but adding those features in can be done
incrementally and without changing the API.

Another neat thing is that this implementation does not specify what types `x`
and `y`, the elements, are. They may be integers, the usual type, but they
could also be any other type that can be hashed and compared. I think most
people end up using a dense representation---an array---with indices, though.
Or maybe the inline embedded pointers approach (see below).

### Tricky bits

You should always refer to the set representative when using union-find. This
means you have to treat `find` kind of like a read barrier in a garbage
collector: using a stale pointer in an operation may be undefined behavior.
The pointer doesn't go bad, exactly---holding onto it is fine---you just need
to call `find` before doing things to it.

This allows other pieces of your infrastructure---say, a type inference
pass---to only store information for and only update the set representative.

<!-- http://users.eecs.northwestern.edu/~mpatwary/SEA2010.pdf -->

<!--

https://www.cs.princeton.edu/courses/archive/spring13/cos423/lectures/UnionFind.pdf

Theorem. [Tarjan-van Leeuwen 1984] Link-by- { size, rank } combined with
{ path compression, path splitting, path halving } performs any intermixed
sequence of m ≥ n find and n – 1 union operations in O(m α(m, n)) time.

-->

### Sentinels and fixpoints

...todo about setting representative to self

## Path splitting

...todo

## Path halving

...todo

## Path compression

```python
def find(x):
    # The same walk as before
    result = x
    while result in uf:
        result = uf[result]
    # Walk the chain again, updating each node to point directly to the end
    while x in uf:
        current = uf[x]
        uf[x] = result
        x = current
    return result
```

## Union by rank

...todo

## Union by index

...todo

## Inline vs out-of-line

For this demonstration of union-find embedded into your existing data
structures, we will use a barebones imaginary compiler IR. This may look
familiar; it is a mashup of Phil Zucker's above union-find and CF
Bolz-Tereick's Toy Optimizer union-find.

```python
class Node:
    forwarded: Node|None = None

    def find(self):
        result = self
        while result.forwarded is not None:
            result = result.forwarded
        return result

    def union(self, other):
        self = self.find()
        other = other.find()
        if self is not other:
            self.forwarded = other

class Add(Node):
    ...
```

## Destructive

Fil Pizlo approach; take no extra space

```python
class Node:
    def find(self):
        result = self
        while isinstance(result, Identity):
            result = result.forwarded
        return result

    def union(self, other):
        self = self.find()
        other = other.find()
        if self is not other:
            self.__class__ = Identity
            self.forwarded = other

class Identity(Node):
    forwarded: Node|None = None

class Add(Node):
    ...
```

This also works without classes; set `opcode` field or something. Just requires
all rewritable nodes be at least as big as `Identity` node, but they probably
are due to having other fields, or alignment, or something.

## Extensions

* ae-graphs (phil zucker and chris fallin; see the end of [my e-graph
  post](/blog/whats-in-an-egraph/))
* e-graphs (see [my e-graph post](/blog/whats-in-an-egraph/))
* annotating edges (groupoids, lesbre/lemerre paper)
* persistence (undo)
* parallelism (win wang)

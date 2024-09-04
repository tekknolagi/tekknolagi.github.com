---
title: "What's in an e-graph?"
layout: post
date: 2024-09-05
---

*This post follows from several conversations with [CF
Bolz-Tereick](https://cfbolz.de/), [Philip
Zucker](https://www.philipzucker.com/), and various e-graphs people.*

I love union-find. It enables fast, easy, in-place IR rewrites for compiler
authors. Its API has two main functions: `union` and `find`. The minimal
implementation is about 15 lines of code and is embeddable directly in your IR.
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

This is really great for some compiler optimizations. Consider the following
made-up IR snippet:

```
v0 = Const 1
v1 = Const 2
v2 = v0 + v1
```

A constant-folding pass can easily determine that `v2` is equivalent to `Const
3` and run `v2.make_equal_to(Const 3)`. This is an unalloyed good: making `v2`
constant might unlock some further optimization opportunities elsewhere and
there's probably no world in which we care to keep the addition representation
of `v2` around.

But not all compiler rewrites are so straightforward and unidirectional.
Consider the expression `(a * 2) / 2`, which is the example from the [e-graphs
good](https://egraphs-good.github.io/) website and paper. A strength reduction
pass might rewrite the `a * 2` subexpression to `a << 1` because left shifts
are often faster than multiplications. That's great; we got a small speedup.

Unfortunately, it stops another hypothetical pass from recognizing that
expressions of the form `(a * b) / b` are equivalent to `a * (b / b)` and
therefore equivalent to `a`. This is because rewrites that use union-find are
eager and destructive; we've gotten rid of the multiplication. How might we
find it again?

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
added two constants:

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

```python
def discover_eclasses(ops: list[Expr]) -> dict[int, set[Expr]]:
    eclasses = {}
    for op in ops:
        found = op.find()
        if found not in eclasses:
            eclasses[found] = set()
        eclasses[found].add(op)
        if op is not found:
            eclasses[op] = eclasses[found]
    return eclasses
```

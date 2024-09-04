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
class Rewritable:
    forwarded: Optional[Rewritable] = dataclasses.field(
        default=None,
        init=False,
        compare=False,
        hash=False,
    )

    def find(self) -> Rewritable:
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
pass might rewrite `a * 2` to `a << 1` because left shifts are often faster
than multiplications. That's great; we got a small speedup.

Unfortunately, it stops another hypothetical pass from recognizing that
expressions of the form `(a * b) / b` are equivalent to `a * (b / b)` and
therefore equivalent to `a`.

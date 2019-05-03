---
title: "Recursive Python objects"
layout: post
date: 2019-05-02 13:24:42 PDT
---

Recently for work I had to check that self-referential Python objects could be
string-ified without endless recursion. In the process of testing my work, I
had to come come up with a way of making self-referential built-in types (eg
`dict`, `list`, `set`, and `tuple`).

Making a self-refential `list` is the easiest task because `list` is just a
dumb mutable container. Make a list and append a reference to itself:

```python
ls = []
ls.append(ls)
>>> ls
[[...]]
>>>
```

`dict` is similarly easy:

```python
d = {}
d['key'] = d
>>> d
{'key': {...}}
>>>
```

Making a self-referential `tuple` is a little bit tricker because tuples cannot
be modified after they are constructed (unless you use the C-API, in which case
this is much easier --- but that's cheating). In order to close the loop, we're
going to have to use a little bit of indirection.

```python
class C:
  def __init__(self):
    self.val = (self,)

  def __repr__(self):
    return self.val.__repr__()

>>> C()
((...),)
>>>
```

Here we create an class that stores a pointer to itself in a tuple. That way
the tuple contains a pointer to an object that contains the tuple ---
`A->B->A`.

The solution is nearly the same for `set`:

```python
class C:
  def __init__(self):
    self.val = set((self,))

  def __repr__(self):
    return self.val.__repr__()

>>> C()
{set(...)}
>>>
```

Note that simpler solutions like directly adding to the set (below) don't work
because `set`s are not hashable, and hashable containers like `tuple` depend on
the hashes of their contents.

```python
s = set()
s.add(s)  # nope
s.add((s,))  # still nope
```

There's not a whole lot of point in doing this, but it was a fun exercise.

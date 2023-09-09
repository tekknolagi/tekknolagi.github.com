---
---

and now for something completely different

i had a nice chat with my friend chris

he walked me through the basics of machine learning while i was looking at
karpathy's micrograd

if you are unfamiliar, micrograd is a very small implementation of a
scalar-valued neural network (as opposed to vectors or matrices as the
computational unit) in pure python (no libraries)

micrograd is a combination of a couple of different and complementary parts:

* a little graph-based expression builder and evaluator
* reverse-mode automatic differentiation on that same computation graph
* neural net building blocks for a multi-layer perceptron

(the thing that got me the first time i read it was that i thought the building
blocks were the network. in this library, no. using a building analogy, they
are more like blueprints. with each evaluation of the network, it is
constructed anew.)

you may be sitting there wondering why i am telling you this

it's because once i untangled and understood the three pieces in micrograd, i
realized

* ml models are graphs
* forward and backward passes are graph traversals
* the graph structure does not change over time
* performance is important

which means... it sounds like a great opportunity for a compiler!

this post is going to be a compiler post, not a machine learning tutorial, so
please treat it as such. maybe it will still help you understand through a
compilers lens.

## intro to the expression builder

the way the expression builder works right now looks like a slightly more
complicated way of doing math in python

```console?lang=python&prompt=>>>,...
>>> from micrograd.engine import Value
>>> a = Value(2)
>>> b = Value(3)
>>> c = Value(4)
>>> d = (a + b) * c
>>> d
Value(data=20, grad=0)
>>>
```

`Value` implements all the operator methods like `__add__ ` to make the process
painless and look as much like math as possible

it's different first because it has this `grad` field---which we'll talk more
about later---but also because as it does the math it also builds up an AST

`Value` instances have a hidden field called `_prev` that stores the
constituent parts that make up an expression

```console?lang=python&prompt=>>>,...
>>> d._prev
{Value(data=5, grad=0), Value(data=4, grad=0)}
>>>
```

they also have an operator

```console?lang=python&prompt=>>>,...
>>> d._op
'*'
>>>
```

we have two operands to the `*`: `c` (4) and `a + b` (5)

it's not quite an AST because it's not a perfect tree; it's expected and normal
to have more of a DAG-like structure

```console?lang=python
>>> w = Value(1)
>>> x = 1 + w
>>> y = 3 * w
>>> z = x + y
>>>
```

`x` and `y` both use `w` and then are both used by `z`, forming a diamond
pattern.

it is assumed that the graph won't have cycles in it

```console?lang=python&prompt=>>>,...
```

## conclusion

ethics note: i don't endorse ml. please don't

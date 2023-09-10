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

we're going to compile micrograd neural nets into C++

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
>>> w = Value(2)
>>> x = 1 + w
>>> y = 3 * w
>>> z = x + y
>>> z
Value(data=9, grad=0)
>>>
```

`x` and `y` both use `w` and then are both used by `z`, forming a diamond
pattern.

it is assumed that the graph won't have cycles in it

right. but why do we have these expression graphs? why not just use math? who
cares about all the back pointers?

well, let's talk about grad.

## let's talk about grad

training a neural network is a process of shaping your function (a neural
network) over time to output the results you want. inside your function are a
bunch of coefficients ("weights") which get iteratively adjusted during
training

the standard process involves building your neural network structure and also
a function that tells you how far off your output is from some expected value
(a "loss function").

if you are trying to get some expected output, you want to minimize the value
of your loss function as much as possible. in order to minimze your loss, you
have to update the weights.

to figure out which weights to update and by how much, you need to know how
much each weight contributes to the final loss. not every weight is equal; some
have significantly more impact than others.

the question "how much did this weight contribute to the loss this round" is
answered by the value of the grad of that weight --- the first derivative. the
slope at a point.

and to compute the grad, you need to traverse backwards from the loss[^forward]
to do something called reverse mode automatic differentiation

[^forward]: there is also forward mode automatic differentiation but i don't
    know much about it and haven't seen it used in my limited search

this sounds complicated but, like evaluating an AST top to bottom, is a tree
traversal with some local state. if you can write a tree-walking interpreter,
you can do reverse mode AD

### reverse mode AD

go from having an expression graph to understanding how each of the component
parts affect the final value (say, loss)

in order to do that you need to take the derivative of each operation and
propagate the grad backwards through the graph toward the weights
("backpropagation")

we do this using the chain rule.

## the chain rule

(i am not going to pretend i am a math person. i vaguely remember the chain rule
from 10 years ago. that's about it. so please look elsewhere for details.)

### a quick overview

the chain rule tells you how to compute derivatives of function composition.
using the example from wikipedia, if you have `h(x) = f(g(x))`, then
`h'(x) = f'(g(x)) * g'(x)`. which is nice, because you don't need to do
anything tricky when you start composing functions, as long as you understand
how to take the derivative of each of the component parts.

for example, if you have `sin(x**2)`, you only need to know the derivative of
`x**2` and `sin(x)` to find out the answer: `cos(x**2) * 2x`

it turns out this comes in handy for taking derivatives of potentially enormous
expression graphs. nobody needs to sit down and work out how to take the
derivative of your huge and no doubt overly complex function... you just have
your building blocks that you already understand, and they are composed.

### applying this to the graph

for a given node, do one step of the chain rule (in pseudocode):

```python
# pseudocode
def backward(node):
    for child in node._prev:
        child.grad += derivative(child) * node.grad
```

instead of just setting `child.grad`, we are increasing it for two reasons:

* one child may be shared with other parents, in which case it affects both
* batching, but that's not important right now

an example of a derivative of a function is (addition? pow? TODO)

we have a function to do one step for one operation node, but we need to do the
whole graph.

but traversing a graph is not as simple as traversing a tree. you need to avoid
visiting a node more than once and also guarantee that you visit child nodes
before parent nodes (in forward mode) or parent nodes before children nodes (in
reverse mode).

for that reason, we have topological sort.

```console?lang=python&prompt=>>>,...
```


## topological sort and graph transformations

wengert list is kind of like TAC or bytecode or IR

## compiling for training vs inference

if you freeze the weights, things get a lot more efficient

## conclusion

ethics note: i don't endorse ml. please don't





i initially compiled neurons/layers/mlp to c++, but it bothered me that adding
new neural network components would require modifying the compiler. so instead
we are compiling the expression graph, which means that all you need to do when
you add a new component is write an interpreter for it

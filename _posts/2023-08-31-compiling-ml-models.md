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

which means... it sounds like a great opportunity for a compiler! this is why
projects like pytorch and tensorflow have compilers. it speeds up both training
and inference. so this post will not contain anything novel. it will contain a
small compiler addition for a small ML engine.

this post is going to be a compiler post, not a machine learning tutorial, so
please treat it as such. maybe it will still help you understand ML through a
compilers lens.

we're going to compile micrograd neural nets into C. in order, we will

* do a brief overview of neural networks
* look at how micrograd does forward and backward passes
* review the chain rule
* learn why micrograd is slow
* write a small compiler

let's go!

## how micrograd does neural networks

first, a bit about multi-layer perceptrons.

<figure style="display: block; margin: 0 auto; max-width: 400px;">
  <img style="max-width: 400px;" src="https://upload.wikimedia.org/wikipedia/commons/b/b8/MultiLayerPerceptron.svg" />
  <figcaption>Fig. 1 - Multi-layer Perceptron diagram. Image courtesy Wikimedia.</figcaption>
</figure>

in this image, circles represent data (neurons) and arrows are operations on
the data. in this case, the red (leftmost) dots are input data. the arrows
going right are multiplications with weights. the meeting of the arrows
represents an addition (forming a dot product).

karpathy implements this pretty directly, with each neuron being an instance of
the `Neuron` class and having a `__call__` method do the dot product. after
each dot product is an activation, in this case `ReLU`, which is equivalent to
`max(x, 0)`.

below is the entire blueprint code for a multilayer perceptron in micrograd:

```python
import random
from micrograd.engine import Value

class Module:

    def zero_grad(self):
        for p in self.parameters():
            p.grad = 0

    def parameters(self):
        return []

class Neuron(Module):

    def __init__(self, nin, nonlin=True):
        self.w = [Value(random.uniform(-1,1)) for _ in range(nin)]
        self.b = Value(0)
        self.nonlin = nonlin

    def __call__(self, x):
        act = sum((wi*xi for wi,xi in zip(self.w, x)), self.b)
        return act.relu() if self.nonlin else act

    def parameters(self):
        return self.w + [self.b]

    def __repr__(self):
        return f"{'ReLU' if self.nonlin else 'Linear'}Neuron({len(self.w)})"

class Layer(Module):

    def __init__(self, nin, nout, **kwargs):
        self.neurons = [Neuron(nin, **kwargs) for _ in range(nout)]

    def __call__(self, x):
        out = [n(x) for n in self.neurons]
        return out[0] if len(out) == 1 else out

    def parameters(self):
        return [p for n in self.neurons for p in n.parameters()]

    def __repr__(self):
        return f"Layer of [{', '.join(str(n) for n in self.neurons)}]"

class MLP(Module):

    def __init__(self, nin, nouts):
        sz = [nin] + nouts
        self.layers = [Layer(sz[i], sz[i+1], nonlin=i!=len(nouts)-1) for i in range(len(nouts))]

    def __call__(self, x):
        for layer in self.layers:
            x = layer(x)
        return x

    def parameters(self):
        return [p for layer in self.layers for p in layer.parameters()]

    def __repr__(self):
        return f"MLP of [{', '.join(str(layer) for layer in self.layers)}]"
```

but this is not done just with floating point numbers. instead he uses this
`Value` thing. what's that about?

## intro to the expression builder

i said that one of micrograd's three components is an expression builder.

the way the expression builder works looks like a slightly more complicated way
of doing math in python

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
painless and look as much like normal Python math as possible

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

it's not quite an AST because it's not a tree; it's expected and normal to have
more of a DAG-like structure

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

so what does that look like in code? well, the `Value.__mul__` function, called
on the left hand side of an `x*y` operation[^binop], looks like this:

[^binop]: Kind of. This is an oversimplification of Python semantics. If you
    want to learn more, check out Brett Cannon's excellent [blog
    post](https://snarky.ca/unravelling-binary-arithmetic-operations-in-python/).

```python
class Value:
    # ...
    def __mul__(self, other):
        # create a transient value if the right hand side is an int or float,
        # like v * 3
        other = other if isinstance(other, Value) else Value(other)
        # pass in data, children, and operation
        out = Value(self.data * other.data, (self, other), '*')
        # ... we'll come back to this part later ...
        return out
```

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

this sounds complicated but, like evaluating an AST top to bottom, reverse mode
AD is a graph traversal with some local state. if you can write a tree-walking
interpreter, you can do reverse mode AD

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

https://web.auburn.edu/holmerr/1617/Textbook/constmultsum-screen.pdf
https://en.wikipedia.org/wiki/Differentiation_rules

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
        child.grad += derivative_wrt_child(child) * node.grad
```

instead of just setting `child.grad`, we are increasing it for two reasons:

* one child may be shared with other parents, in which case it affects both
* batching, but that's not important right now

<!--
an example of a derivative of a function is (addition? pow? TODO)
-->

let's take a look at karpathy's implementation of the derivative of `*`, for
example. in math first: if you have `f(x,y) = x*y`, then `df/dx = 1*y`. now
in code:

```python
class Value:
    # ...
    def __mul__(self, other):
        other = other if isinstance(other, Value) else Value(other)
        out = Value(self.data * other.data, (self, other), '*')

        def _backward():
            self.grad += other.data * out.grad
            other.grad += self.data * out.grad
        out._backward = _backward

        return out
```

see what a nice translation of the math that is? get the derivative, apply the
chain rule, add to the grad.

now we have a function to do one step for one operation node, but we need to do
the whole graph.

but traversing a graph is not as simple as traversing a tree. you need to avoid
visiting a node more than once and also guarantee that you visit child nodes
before parent nodes (in forward mode) or parent nodes before children nodes (in
reverse mode).

for that reason, we have topological sort.

```console?lang=python&prompt=>>>,...
```


## topological sort and graph transformations

a topological sort on a graph builds an order where children are always visited
before their parents. (in general this only works if the graph does not have
cycles, but---thankfully---we already assume above that the graph does not have
cycles.)

```python
class Value:
    # ...
    def topo(self):
        # modified from Value.backward, which builds a topological sort
        # internally
        topo = []
        visited = set()
        def build_topo(v):
            if v not in visited:
                visited.add(v)
                for child in v._prev:
                    build_topo(child)
                topo.append(v)
        build_topo(self)
        return topo
```

for example, we can do a topological sort of a very simple expression graph,
`1+2`.

```console?lang=python&prompt=>>>,...
>>> from micrograd.engine import Value
>>> x = Value(1)
>>> y = Value(2)
>>> z = x + y
>>> z.topo()
[Value(data=1, grad=0), Value(data=2, grad=0), Value(data=3, grad=0)]
>>>
```

the topological sort says that in order to calculate the value `3`, we must
first calculate the values `1` and `2`. it doesn't matter in what order we do
`1` and `2`, but they both have to come before `3`.

### applying this to backpropagation

if we take what we know now about the chain rule and topological sort, we can
do backpropagation on the graph. this is the code straight from micrograd,
1) builds a topological sort, and 2) operates on it in reverse, applying the
chain rule to each `Value` one at a time.

```python
class Value:
    # ...
    def backward(self):

        # topological order all of the children in the graph
        topo = []
        visited = set()
        def build_topo(v):
            if v not in visited:
                visited.add(v)
                for child in v._prev:
                    build_topo(child)
                topo.append(v)
        build_topo(self)

        # go one variable at a time and apply the chain rule to get its gradient
        self.grad = 1
        for v in reversed(topo):
            v._backward()
```

this is normally called on the result `Value` of the loss function.

<!--
linearize the operations both for forward and backward passes

wengert list is kind of like TAC or bytecode or IR
-->

## putting it all together

i am not going to get into the specifics, but here is what a rough sketch of
very simplified training loop might look like for MLP-based classifier for the
MNIST digit recognition problem:

(to be clear, this code is not runnable as-is. it needs the image loading
support code and a loss function. the full code is available in the GitHub
repo.)

```python
import random
from micrograd.nn import MLP
# ...
NUM_DIGITS = 10
LEARNING_RATE = 0.1
# each image is 28x28. hidden layer of width 50. 10 digits output.
model = MLP(28*28, [50, NUM_DIGITS])
# pretend there is some kind of function that loads the labeled training images
# into memory
db = list(images("train-images-idx3-ubyte", "train-labels-idx1-ubyte"))
num_epochs = 100
for epoch in range(num_epochs):
    for image in db:
        # zero grad
        for p in model.parameters():
            p.grad = 0.0
        # forward
        output = model(image.pixels)
        loss = compute_loss(output)
        # backward
        loss.backward()
        # update
        for p in model.parameters():
            p.data -= LEARNING_RATE * p.grad
```

the `MLP` builds a bunch of `Neuron`s in `Layer`s and initializes some weights
as `Value`s, but it does not construct the graph yet. only when it is called
(as in `model(image.pixels)`) does it construct the graph and do all of the dot
products. then we construct more of the graph on top of that when calculating
the loss.

this is nice and simple---thank you, Andrej---but is it fast enough to be
usable? let's find out.

## performance problems

uh oh, running this with cpython is slow. it looks like computing a forward
pass for one image takes about a second. and then we have to do a backward
pass, too. that is way too long!

obvious solution: try with pypy. oh neat, a couple per second. not enough.

(btw, skybison is way faster! fun fact. its major perf pain point was function
creation (that is a bit slow in skybison right now), but if you lift the
lambdas to the top level that goes away. then it's very clear that set lookup
from topo sort is the slowest bit in the profile. then it's garbage collection
from all the transient objects.

incidentally, hoisting the lambdas to be normal functions also massively speeds
up pypy and it becomes faster than skybison.)

hypothesis for pain points:

* recreating the graph with every forward pass (allocation)
* doing a topological sort with every backward pass (pointer chasing, function
  calls, allocation)
* python interpreter stuff

but instead of optimizing blindly in the dark, we should measure.

### checking with a profiler

use scalene. see massive memory allocations on `sum(...)` and `set(_children)`

### solutions

solutions (respectively):

* re-use the old graph. just copy in new inputs
* since you aren't changing the graph, no need to re-topo-sort. keep the
  ordering around, too. this helps for both forward and backward passes.
* compile the topo sort with its operations to C or something

as usual with compilers, if you can freeze some of the dynamism in the
allowable semantics of a program you get a performance benefit. since the graph
shape is static, this sounds like a fine idea.

<!--
TODO: parallelization of work in the graph? is that possible? it looks like in
MNIST you can't do anything for tensor version, but maybe for scalar.
-->

## let's write a compiler

the goal with this compiler is to write something very small that fits
reasonably cleanly into micograd as it already is---not to re-architect
anything.

the original version of this project compiled the `MLP` directly into C, but
that unfortunately is not very extensible: making architectural changes to your
model would then require writing new compilers.

for this reason, we are writing compilers of `Value` graphs. this means anybody
get a compiler for free as long as their machine learning architecture uses
`Value`s. you need only write an interpreter for it!

### forward

since we have a topological sort, we might as well use it. then we only need to
write a compiler that works one `Value` at a time. then we can drive it like
this:

```console?lang=python&prompt=>>>,...
>>> from micrograd.engine import Value
>>> x = Value(1)
>>> y = Value(2)
>>> z = x + y
>>> order = z.topo()
>>> for v in order:
...     print(v.compile())
...
data[1] = 2;
data[0] = 1;
data[2] = data[1]+data[0];
>>>
```

where it is assumed that `data` is some properly-sized array of `double`s that
we will create later.

look, there it is! a neat little linearization of the graph. this strategy
works because we don't have loops and we don't have re-definitions of values.
each value is set once[^ssa]. and this code, even with all its memory loads
and stores, should be much faster than pointer chasing and function calls
in Python-land.

[^ssa]: This makes it SSA form by definition!

```python
class Value:
    # ...
    def var(self):
        return f"data[{self._id}]"

    def set(self, val):
        return f"{self.var()} = {val};"

    def compile(self):
        if self._op in ('weight', 'bias', 'input'):
            # Not calculated; set elsewhere
            return ""
        if self._op == '':
            return self.set(f"{self.data}")
        if self._op == '+':
            c0, c1 = self._prev
            return self.set(f"{c0.var()}+{c1.var()}")
        raise RuntimeError(f"op {self._op} left as an exercise for the reader")
```

the other operators are not so different. see if you can figure out how to
implement `**` or `exp`, for example. note that `**` requires either storing
additional data or a kind of gross hack.

you may notice that this requires assigning names to `Value`s. for this, we
have added an `_id` field that is an auto-incrementing counter in the
`__init__` function. the implementation does not matter so much.

my complete compiler implementation is about 40 lines and it even includes some
small on-the-fly optimizations.

this compiler does forward passes. what about backward passes? that has to be
much more complicated, right?

### backward

actually, it's about the same complexity. we need only do a line-by-line
translation of the backpropagation functions (all the `_backward`
implementations).

for example, we can revisit the backpropagation for `*`. i added some helper
functions to make the code shorter and look more like the interpreted version.

```python
class Value:
    # ...
    def getgrad(self):
        if self._op in ('', 'input'):
            raise RuntimeError("Grad for constants and input data not stored")
        if self._op in ('weight', 'bias'):
            return f"grad[{self._id}]"
        return f"grad{self._id}"

    def setgrad(self, val):
        if self._op in ('', 'input'):
            return []
        return [f"{self.getgrad()} += {val};"]

    def backward_compile(self):
        if not self._prev:
            assert self._op in ('', 'weight', 'bias', 'input')
            # Nothing to propagate to children.
            assert not self._prev
            return []
        if self._op == '*':
            left, right = self._prev
            return left.setgrad(f"{right.var()}*{self.getgrad()}") +\
                    right.setgrad(f"{left.var()}*{self.getgrad()}")
        raise RuntimeError(f"op {self._op} left as an exercise for the reader")
```

where it is assumed that `grad` is some properly-sized array of `double`s that
we will create later.

my complete backward pass compiler implementation is about 30 lines! shorter
than the forward pass, even.

### update

<!-- TODO -->

```python
def gen_update(f, model, learning_rate):
    for o in model.parameters():
        assert o._op in ('weight', 'bias'), repr(o._op)
        print(f"data[{o._id}] -= {learning_rate} * {o.getgrad()};", file=f)
```

<!-- TODO -->

it's even the same length as the Python equivalent, if you exclude the
`assert`.

### setting the input

<!-- TODO -->

### a python c extension

having a bunch of free-floating code to update `data` and `grad` arrays is fun,
but it's not a complete compiler. we need to wrap that code in functions (i
called them `forward`, `backward`, `update`, and `set_input`) and make them
accessible to our Python driver program. we don't want to have to completely
move to C!

<!-- TODO -->

## compiling for training vs inference

if you freeze the weights, things get a lot more efficient. right now we have
so many memory loads and stores and it's hard for the C compiler to prove
anything about the properties of the numbers when it is trying to optimize. it
probably also prevents SIMD. does the lack of locality hurt too?

## conclusion

ethics note: i don't endorse ml. please don't

## more thonks

### scalar-valued is less efficient than tensor-valued

we managed to remove a lot of the overhead *for the program we had*, but the
overall architecture did not improve. to do that, we need to move from
scalar-valued to tensor-valued.

it's kind of like programming in assembly vs a higher level language. it's much
harder to make optimizations directly on the assembly. whereas if you have
three bigger and more descriptive operations in your ast (`matmul`, etc), the
compiler can better understand what you mean and optimize that.

it also brings better data locality (matrix is stored densely) and we can get
some vectorized math instead of millions of `mulsd`.

* fuse matmul with addition of bias (`W @ x + b`)
  * https://discuss.tvm.apache.org/t/operator-fusion-for-rnn/11966
  * https://github.com/pytorch/pytorch/issues/39661
* fuse matmul/add with activation function (`relu(W @ x + b)`)
  * https://github.com/pytorch/pytorch/issues/77171
* matmul associativity (and commutativity with einstein notation??) to reduce
  size of intermediate values

<!--
TODO: parallelization of matmul? GEMM?
-->

### what if you wrote micrograd in rpython?

could PyPy jit it effectively?

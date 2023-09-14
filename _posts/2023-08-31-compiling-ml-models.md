---
---

I had a nice chat with my friend [Chris](https://www.chrisgregory.me/)
recently.

He walked me through the basics of machine learning while I was looking at
[Andrej Karpathy](https://karpathy.ai/)'s
[micrograd](https://github.com/karpathy/micrograd/).

If you are unfamiliar, micrograd is a very small implementation of a
scalar-valued neural network (as opposed to vectors or matrices as the
computational unit) in pure Python, which uses no libraries.

Micrograd is a combination of a couple of different and complementary parts:

* a little graph-based expression builder and evaluator
* reverse-mode automatic differentiation on that same computation graph
* neural net building blocks for a multi-layer perceptron (MLP)

Together, this lets you write code that looks like this:

```python
from micrograd.nn import MLP
model = MLP(2, [4, 1])
```

And summon a neural network from thin air.

The thing that got me the first time I read it was that I thought the building
blocks *were* the network. In this library, no. Using a building analogy, they
are more like blueprints or scaffolding. With each evaluation of the network,
the network and intermediate computation graph is constructed anew. In compiler
terms, the building blocks are kind of like the front-end and the expression
graph is a sort of intermediate representation (IR).

You may be sitting there wondering why I am telling you this. I normally blog
about compilers. What's this?

It's because once I untangled and understood the three pieces in micrograd, I
realized:

* ML models are graphs
* forward and backward passes are graph traversals
* the graph structure does not change over time
* performance is important

Which means... it sounds like a great opportunity for a compiler! This is why
projects like PyTorch and TensorFlow have compilers (torchdynamo, XLA, etc).
Compiling your model speeds up both training and inference. So this post will
not contain anything novel---it's hopefully a quick sketch of a small example
of what the Big Projects do.

> This post is going to be a compiler post, not a machine learning tutorial, so
> please treat it as such. Maybe it will still help you understand ML through a
> compilers lens.

We're going to compile micrograd neural nets into C. In order, we will

* do a brief overview of neural networks
* look at how micrograd does forward and backward passes
* review the chain rule
* learn why micrograd is slow
* write a small compiler
* see micrograd go zoom

Let's go!

## how micrograd does neural networks

First, a bit about multi-layer perceptrons. MLPs are densely connected neural
networks where input flows in one direction through the network. As it exists
in the upstream repository, micrograd only supports MLPs.

In case visual learning is your thing, here is a small diagram:

<figure style="display: block; margin: 0 auto; max-width: 400px;">
  <img style="max-width: 400px;" src="https://upload.wikimedia.org/wikipedia/commons/b/b8/MultiLayerPerceptron.svg" />
  <figcaption>Fig. 1 - Multi-layer Perceptron diagram. Image courtesy Wikimedia.</figcaption>
</figure>

<!-- TODO maybe use mine instead -->

<figure style="display: block; margin: 0 auto; max-width: 400px;">
  <object class="svg" type="image/svg+xml" data="/assets/img/nn.svg">
  If you're seeing this text, it means your browser cannot render SVG.
  </object>
  <figcaption>Fig. 1 - Multi-layer Perceptron diagram. I made this in
  Excalidraw. I love Excalidraw.</figcaption>
</figure>

<!-- or maybe just as a closer look -->

In this image, circles represent data (input or intermediate computation
results) and arrows are weights and operations on the data. In this case, the
red (leftmost) dots are input data. The arrows going right are multiplications
with weights. The meeting of the arrows represents an addition (forming a dot
product).

Karpathy implements this pretty directly, with each neuron being an instance of
the `Neuron` class and having a `__call__` method do the dot product. After
each dot product is an activation, in this case `ReLU`, which is equivalent to
`max(x, 0)`.

Below is the entire blueprint code for a multilayer perceptron in micrograd:

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

You can ignore some of the clever coding in `MLP.__init__`. This ensures that
all of the layers match up end-to-end dimension-wise.

But this neural network is not build just with floating point numbers. Instead
he uses this `Value` thing. What's that about?

## Intro to the expression builder

I said that one of micrograd's three components is an expression graph builder.

Using the expression builder looks like a slightly more complicated way of
doing math in Python:

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

The `Value` class even implements all the operator methods like `__add__ ` to
make the process painless and look as much like normal Python math as possible.

But it's a little different than normal math. It's different first because it
has this `grad` field---which we'll talk more about later---but also because as
it does the math it also builds up an graph (you can kind of think of it as an
abstract syntax tree, or AST).

It's not visible in the normal string representation, though. `Value` instances
have a hidden field called `_prev` that stores the constituent parts that make
up an expression:

```console?lang=python&prompt=>>>,...
>>> d._prev
{Value(data=5, grad=0), Value(data=4, grad=0)}
>>>
```

They also have a hidden operator field:

```console?lang=python&prompt=>>>,...
>>> d._op
'*'
>>>
```

This means that we have two operands to the `*` node `d`: `c` (4) and `a + b`
(5).

I said you could think about it like an AST but it's not quite an AST because
it's not a tree. It's expected and normal to have more of a directed acyclic
graph (DAG)-like structure.

```console?lang=python
>>> w = Value(2)
>>> x = 1 + w
>>> y = 3 * w
>>> z = x + y
>>> z
Value(data=9, grad=0)
>>>
```

Here `x` and `y` both use `w` and then are both used by `z`, forming a diamond
pattern.

<svg width="204pt" height="188pt"
 viewBox="0.00 0.00 203.57 188.00" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">
<g id="graph0" class="graph" transform="scale(1 1) rotate(0) translate(4 184)">
<title>G</title>
<polygon fill="#ffffff" stroke="transparent" points="-4,4 -4,-184 199.5659,-184 199.5659,4 -4,4"/>
<!-- z -->
<g id="node1" class="node">
<title>z</title>
<ellipse fill="none" stroke="#000000" cx="98.7986" cy="-162" rx="40.9005" ry="18"/>
<text text-anchor="middle" x="98.7986" y="-157.8" font-family="Times,serif" font-size="14.00" fill="#000000">z = x+y</text>
</g>
<!-- x -->
<g id="node2" class="node">
<title>x</title>
<ellipse fill="none" stroke="#000000" cx="45.7986" cy="-90" rx="45.5981" ry="18"/>
<text text-anchor="middle" x="45.7986" y="-85.8" font-family="Times,serif" font-size="14.00" fill="#000000">x = 1+w </text>
</g>
<!-- z&#45;&gt;x -->
<g id="edge1" class="edge">
<title>z&#45;&gt;x</title>
<path fill="none" stroke="#000000" d="M85.9688,-144.5708C79.5712,-135.8797 71.7098,-125.2001 64.6429,-115.5998"/>
<polygon fill="#000000" stroke="#000000" points="67.4409,-113.4968 58.694,-107.5182 61.8035,-117.6465 67.4409,-113.4968"/>
</g>
<!-- y -->
<g id="node3" class="node">
<title>y</title>
<ellipse fill="none" stroke="#000000" cx="152.7986" cy="-90" rx="42.5359" ry="18"/>
<text text-anchor="middle" x="152.7986" y="-85.8" font-family="Times,serif" font-size="14.00" fill="#000000">y = 3*w</text>
</g>
<!-- z&#45;&gt;y -->
<g id="edge2" class="edge">
<title>z&#45;&gt;y</title>
<path fill="none" stroke="#000000" d="M111.8705,-144.5708C118.4371,-135.8153 126.5173,-125.0418 133.7586,-115.3867"/>
<polygon fill="#000000" stroke="#000000" points="136.6471,-117.3687 139.8471,-107.2687 131.0471,-113.1687 136.6471,-117.3687"/>
</g>
<!-- w -->
<g id="node4" class="node">
<title>w</title>
<ellipse fill="none" stroke="#000000" cx="98.7986" cy="-18" rx="33.2211" ry="18"/>
<text text-anchor="middle" x="98.7986" y="-13.8" font-family="Times,serif" font-size="14.00" fill="#000000">w = 2</text>
</g>
<!-- x&#45;&gt;w -->
<g id="edge4" class="edge">
<title>x&#45;&gt;w</title>
<path fill="none" stroke="#000000" d="M58.6284,-72.5708C65.1682,-63.6866 73.2376,-52.7245 80.4242,-42.9615"/>
<polygon fill="#000000" stroke="#000000" points="83.3424,-44.9012 86.4519,-34.7729 77.705,-40.7514 83.3424,-44.9012"/>
</g>
<!-- y&#45;&gt;w -->
<g id="edge3" class="edge">
<title>y&#45;&gt;w</title>
<path fill="none" stroke="#000000" d="M139.7267,-72.5708C133.0636,-63.6866 124.842,-52.7245 117.5197,-42.9615"/>
<polygon fill="#000000" stroke="#000000" points="120.1783,-40.6729 111.3783,-34.7729 114.5783,-44.8729 120.1783,-40.6729"/>
</g>
</g>
</svg>

It is assumed that the graph won't have cycles in it[^rnn-unrolling].

[^rnn-unrolling]: Apparently even recurrent neural networks (RNNs) are "loop
    unrolled" meaning they copy and paste the structure in the IR instead of
    having an actual looping structure.

So what does creating the graph look like in code? well, the `Value.__mul__`
function, called on the left hand side of an `x*y` operation[^binop], looks
like this:

[^binop]: Kind of. This is an oversimplification of Python semantics. If you
    want to learn more, check out Brett Cannon's excellent [blog
    post](https://snarky.ca/unravelling-binary-arithmetic-operations-in-python/).

```python
class Value:
    # ...
    def __mul__(self, other):
        # create a transient value if the right hand side is a constant int or
        # float, like v * 3
        other = other if isinstance(other, Value) else Value(other)
        # pass in new data, children, and operation
        out = Value(self.data * other.data, (self, other), '*')
        # ... we'll come back to this hidden part later ...
        return out
```

The `children` tuple `(self, other)` are the pointers to the other nodes in the
graph.

But why do we have these expression graphs? Why not just use math? Who
cares about all the back pointers?

## Let's talk about grad

Training a neural network is a process of shaping your function (the neural
network) over time to output the results you want. Inside your function are a
bunch of coefficients ("weights") which get iteratively adjusted during
training.

The standard training process involves your neural network structure and also
another function that tells you how far off your output is from some expected
value (a "loss function"). An simple example of a loss function is
`loss(actual, expected) = (expected - actual)**2`. If you use this particular
function across multiple inputs at a time, it's called Mean Squared Error
(MSE).

If you are trying to get some expected output, you want to minimize the value
of your loss function as much as possible. In order to minimze your loss, you
have to update the weights.

To figure out which weights to update and by how much, you need to know how
much each weight contributes to the final loss. Not every weight is equal; some
have significantly more impact than others.

The question "how much did this weight contribute to the loss this round" is
answered by the value of the grad of that weight---the first derivative. The
slope at a point. For example, in `y = mx + b`, the equation that describes a
line, the derivative with respect to `x` is `m`, because the value of `x` is
scaled by `m`.

To compute the grad, you need to traverse backwards from the loss[^forward] to
do something called reverse mode automatic differentiation (reverse mode AD).
This sounds scary. Every article online about it has scary notation and
squiggly lines. But it's pretty okay, actually, so keep on reading.

[^forward]: There is also forward mode automatic differentiation but I don't
    know much about it and haven't seen it used in my limited search.

Fortunately for us, reverse mode AD, like evaluating an AST top to bottom, it
is a graph traversal with some local state. If you can write a tree-walking
interpreter, you can do reverse mode AD.

### Reverse mode AD

<!-- TODO -->

go from having an expression graph to understanding how each of the component
parts affect the final value (say, loss)

in order to do that you need to take the derivative of each operation and
propagate the grad backwards through the graph toward the weights
("backpropagation")

we do this using the chain rule.

## The chain rule

I am not going to pretend that I am a math person. Aside from what I re-learned
in the last couple of weeks, I only vaguely remember the chain rule from 10
years ago. Most of what I remember is my friend Julia figuring it out
instantaneously and wondering why I didn't get it yet. That's about it. So
please look elsewhere for details if this section doesn't do it for you. I
won't be offended.

### A quick overview

The chain rule tells you how to compute derivatives of function composition.
Using the example from Wikipedia, if you have some function `h(x) = f(g(x))`,
then `h'(x) = f'(g(x)) * g'(x)` (where `f'` and `h'` and `g'` are the
derivatives of `f` and `h`' and `g`, respectively). This rule is nice, because
you don't need to do anything tricky when you start composing functions, as
long as you understand how to take the derivative of each of the component
parts.

For example, if you have `sin(x**2)`, you only need to know the derivative of
the component functions `x**2` (it's `2*x`) and `sin(x)` (it's `cos(x)`) to
find out the answer: `cos(x**2) * 2x`.

To take a look at the proof of this and also practice a bit, take a look at
[this short slide
deck](https://web.auburn.edu/holmerr/1617/Textbook/chainrule-screen.pdf) (PDF)
from Auburn University. Their course page [table of
contents](https://web.auburn.edu/holmerr/1617/Textbook/contents.aspx) has more
slide decks[^pdf-nav].

[^pdf-nav]: Side note, I have never seen navigation in a PDF like that. It's so
    neat.

Also make sure to check out the [list of differentiation
rules](https://en.wikipedia.org/wiki/Differentiation_rules) on Wikipedia.

It turns out that the chain rule comes in handy for taking derivatives of
potentially enormous expression graphs. Nobody needs to sit down and work out
how to take the derivative of your huge and no doubt overly complex function...
you just have your building blocks that you already understand, and they are
composed.

So let's apply the chain rule to expression graphs.

### Applying this to the graph

We'll start with one `Value` node at a time. For a given node, we can do one
step of the chain rule (in pseudocode):

```python
# pseudocode
def backward(node):
    for child in node._prev:
        child.grad += derivative_wrt_child(child) * node.grad
```

Where `wrt` means "with respect to". It's important that we take the derivative
of each child *with respect to the child*.

Instead of just setting `child.grad`, we are increasing it for two reasons:

* one child may be shared with other parents, in which case it affects both
* batching, but that's not important right now

<!--
an example of a derivative of a function is (addition? pow? TODO)
-->

To make this more concrete, let's take a look at Karpathy's implementation of
the derivative of `*`, for example. In math, if you have `f(x,y) = x*y`, then
`f'(x, y) = 1*y`. In code, that looks like:

```python
class Value:
    # ...
    def __mul__(self, other):
        other = other if isinstance(other, Value) else Value(other)
        out = Value(self.data * other.data, (self, other), '*')

        # The missing snippet from earlier!
        def _backward():
            self.grad += other.data * out.grad
            other.grad += self.data * out.grad
        out._backward = _backward

        return out
```

This means that for each of the children, we will use the *other child*'s data
and (because of the chain rule) multiply it by the parent expression's `grad`.
See what a nice translation of the math that is? Get the derivative, apply the
chain rule, add to the child's grad.

Now we have a function to do one derivative step for one operation node, but we
need to do the whole graph.

But traversing a graph is not as simple as traversing a tree. You need to avoid
visiting a node more than once and also guarantee that you visit child nodes
before parent nodes (in forward mode) or parent nodes before children nodes (in
reverse mode).

For that reason, we have topological sort.

## Topological sort and graph transformations

A topological sort on a graph is an order where children are always visited
before their parents. In general this only works if the graph does not have
cycles, but---thankfully---we already assume above that the graph does not have
cycles.

Here is a sample topological sort on the `Value` graph. It uses the nested
function `build_topo` for terseness, but that is not strictly necessary.

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

To get a feel for how this works, we can do a topological sort of a very simple
expression graph, `1+2`.

```console?lang=python&prompt=>>>,...
>>> from micrograd.engine import Value
>>> x = Value(1)
>>> y = Value(2)
>>> z = x + y
>>> z.topo()
[Value(data=1, grad=0), Value(data=2, grad=0), Value(data=3, grad=0)]
>>>
```

The topological sort says that in order to calculate the value `3`, we must
first calculate the values `1` and `2`. It doesn't matter in what order we do
`1` and `2`, but they both have to come before `3`.

Now that we have a way to get a graph traversal order, we can start doing some
backpropagation.

### Applying this to backpropagation

If we take what we know now about the chain rule and topological sort, we can
do backpropagation on the graph. Below is the code straight from micrograd. It
first builds a topological sort and then operates on it in reverse, applying
the chain rule to each `Value` one at a time.

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

        # --- the new bit ---
        # go one variable at a time and apply the chain rule to get its gradient
        self.grad = 1
        for v in reversed(topo):
            v._backward()
```

The `.backward()` function is normally called on the result `Value` of the loss
function.

If you are wondering why we set `self.grad` to `1` here before doing
backpropagation, take a moment and wonder to yourself. Maybe it's worth drawing
a picture!

<!--
linearize the operations both for forward and backward passes

wengert list is kind of like TAC or bytecode or IR
-->

## Putting it all together

I am not going to get into the specifics, but here is what a rough sketch of
very simplified training loop might look like for MLP-based classifier for the
MNIST digit recognition problem. **This code is not runnable as-is.** It needs
the image loading support code and a loss function. The full code is available
in the GitHub repo.

```python
import random
from micrograd.nn import MLP
# ...
NUM_DIGITS = 10
LEARNING_RATE = 0.1
# Each image is 28x28. Hidden layer of width 50. Output 10 digits.
model = MLP(28*28, [50, NUM_DIGITS])
# Pretend there is some kind of function that loads the labeled training images
# into memory.
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

In this snippet, the `MLP` builds a bunch of `Neuron`s in `Layer`s and
initializes some weights as `Value`s, but it does not construct the graph yet.
Only when it is called (as in `model(image.pixels)`) does it construct the
graph and do all of the dot products. Then we construct more of the graph on
top of that when calculating the loss. This is the forward pass!

This is nice and simple---thank you, Andrej---but is it fast enough to be
usable? Let's find out.

## Performance problems

Uh oh, running this with CPython is slow. It looks like computing a forward
pass for one image takes about a second. And then we have to do a backward
pass, too. And we have to do several epochs of all 60,000 images. That is going
to take way too long!

Well, let's do what everyone always suggests: try with
[PyPy](https://www.pypy.org/). Oh neat, a couple images per second.
Unfortunately, that is still not fast enough.

> By the way, our old project
> [Skybison](https://github.com/tekknolagi/skybison) is way faster! What a fun
> fact. After some profiling, its major performance pain point was function
> creation (that is a bit slow in Skybison right now), but if you lift the
> `_backward` lambdas to the top level, the problem goes away. Then it's very
> clear that set lookup from topo sort is the slowest bit in the profile. After
> that it's garbage collection from all the transient `Value` objects.

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

## did it work? is it faster?

<!-- TODO -->

holy cow, 2000x (~2000 images/s)

<!--

### more optimizations

TODO: is this true? does it work?

i added `-O1` but Clang took forever so I eventually killed the process. so i
tried to figure out what was taking so long using gdb and repeatedly attaching
and it looked like SROA (scalar replacement of aggregates). so i took
everything out of arrays and put them in individual global variables. then
regalloc took a long time (but not forever!) and eventually, in 373s instead of
30s, i got a binary. that binary runs 2x as fast (~4000 images/s).

-->

## conclusion

ethics note: i don't endorse ml. please don't

## more thonks

### compiling for training vs inference

if you freeze the weights, things get a lot more efficient. right now we have
so many memory loads and stores and it's hard for the C compiler to prove
anything about the properties of the numbers when it is trying to optimize. it
probably also prevents SIMD. does the lack of locality hurt too?

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

### what if you generated python code or bytecode?

could PyPy jit it effectively?
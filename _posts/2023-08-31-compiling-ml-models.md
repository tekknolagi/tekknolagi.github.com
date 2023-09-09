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

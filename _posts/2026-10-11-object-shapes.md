---
title: "Objects, attributes, maps, oh my!"
layout: post
---

Or, *IC UC we all C for ICs*.

In many dynamically typed programming languages, objects look like a big ol'
mapping of names to values. The languages encourage this flexibility. Consider
for example Python:

```python
class C:
    def __init__(self):
        self.a = 1
        self.b = 2
```

Or Ruby:

```ruby
class C
  def initialize
    @a = 1
    @b = 2
  end
end
```

Or JavaScript[^js-synonyms]:

[^js-synonyms]: Or the following equivalents...

    ```javascript
    // Or
    function C() {
        this.a = 1;
        this.b = 2;
    }
    // Or
    class C {
        constructor() {
            this.a = 1;
            this.b = 2;
        }
    }
    ```

```javascript
let obj = {};
obj.a = 1;
obj.b = 2;
```

This might encourage the VM implementor to model object attributes using a hash
table. Hash tables, after all, are very good at mapping names to values.

Here is, by way of example, a Python sketch of what this object model looks
like. Because it is a skethc, it uses `write_attr` and `read_attr` instead of
the much more common syntax that your programming language comes with.

```python
MISSING = object()

class Instance:
    def __init__(self):
        # TODO: the rest of your object system
        self.cls = ...
        self._fields = {}

    def read_attr(self, fieldname):
        return self._fields.get(fieldname, MISSING)

    def write_attr(self, fieldname, value):
        self._fields[fieldname] = value

obj = Instance()
obj.write_attr("a", 1)
obj.write_attr("b", 2)
print(obj.read_attr("a") + obj.read_attr("b"))
```

This has its benefits:

* it is straightforward to understand
* ... uh, well, maybe that's it.

It also has its drawbacks:

* it uses a hash table for a very common operation: while this is technically
  O(1), hashing and probing are very slow
* it has an indirection: jumping from an object to its separate hash table
  field storage requires a lot of dependent memory loads. object attributes are
  accessed frequently and should therefore be stored in-line on the object
  instead of another memory access (and potentially cache miss) away

<figure>
<object class="svg" type="image/svg+xml" data="/assets/img/hashtable-memory.excalidraw.svg"></object>
<figcaption>This is a pretty good case where the hash table doesn't have its own internal
fragmentation.</figcaption>
</figure>

This means that literally every operation `a.b` is doing hundreds or thousands
of CPU instructions, including multiple dependent memory loads. This is *not
good* for performance.

So runtime implementors tend to implement this thing called maps, or layouts,
or shapes, or hidden classes, or structures, or probably a secret sixth name I
don't know about, which optimizes reading, writing, and storage of attributes.

But what does this shape code look like? How does it actually help performance?
A lot of big production runtimes have pretty hairy code with domain-specific
edge cases so they may not help get a handle on the basics. And a lot of blog
posts, while excellent high-level overviews, are lacking in implementation. So
here we go once more, blogging where everyone else has already blogged before.
With any luck, you should be able to write a page of code and hold the whole
thing in your head at once.

## 500 lines or less

I decided to once more give CF Bolz-Tereick's [A Simple Object
Model](https://aosabook.org/en/500L/a-simple-object-model.html) the thorough
read it deserves. In the past, I tried a couple times and bounced off of it
because it models Python with reasonable fidelity and the Python object model
is *really hairy*.

But did I read it thoroughly this time? Well, no. This time I allowed myself to
skim the class- and method-related parts and instead focus on the instance
attribute and map parts. Inside CF's post is the core of a very clean map
system and I wanted to experiment with that, so I extracted it out.

CF's post starts off very similar to our own sketch, modeling objects as
unordered maps of attributes. It also fills in some of the TODO stuff I left
out earlier around classes. I'll copy my reduced implementation of it here so
you can take a gander:

```python
# The base object model code:

class Base:
    def __init__(self, cls, fields):
        self.cls = cls
        self._fields = fields

    def read_attr(self, fieldname):
        return self._read_dict(fieldname)

    def write_attr(self, fieldname, value):
        return self._write_dict(fieldname, value)

    def _read_dict(self, fieldname):
        return self._fields.get(fieldname, MISSING)

    def _write_dict(self, fieldname, value):
        self._fields[fieldname] = value

MISSING = object()

class Instance(Base):
    def __init__(self, cls: "Class"):
        super().__init__(cls, {})

class Class(Base):
    def __init__(self, name, base_class, fields, metaclass):
        super().__init__(metaclass, fields)
        self.name = name
        self.base_class = base_class

OBJECT = Class(name="object", base_class=None, fields={}, metaclass=None)
TYPE = Class(name="type", base_class=OBJECT, fields={}, metaclass=None)
TYPE.cls = TYPE
OBJECT.cls = TYPE
```

Which you can use like this (test code also taken from CF's post):

```python
A = Class(name="A", base_class=OBJECT, fields={}, metaclass=TYPE)
obj = Instance(A)
obj.write_attr("a", 1)
assert obj.read_attr("a") == 1

obj.write_attr("b", 5)
assert obj.read_attr("a") == 1
assert obj.read_attr("b") == 5

obj.write_attr("a", 2)
assert obj.read_attr("a") == 2
assert obj.read_attr("b") == 5
```

There's a bit of delegation going on in the `Base` class because the post
already anticipates what we're about to do: swap out the `Instance`
implementation for `_read_dict` and `_write_dict` for better performance.

## Cartography

OK, let's add on this "map" data structure. The purpose of the map is to do
three things:

1. Get out of the business of dealing with strings and hashing in the fast
   path. We would like to instead do operations on small integers.
2. Remove the hash table indirection. We would like to store the attributes
   in-line in the object.
3. Be/have a stable idenfier that describes a particular object layout.

**For the former**, we can think of an object's map field as an (database) index or
a write-through cache. It is responsible for reflecting on the layout of a
given object:

<figure>
<object class="svg" type="image/svg+xml" data="/assets/img/hashtable-shape-memory.excalidraw.svg"></object>
<figcaption>This model is <i>mostly</i> there...</figcaption>
</figure>

You can ask the map questions like "what attributes does this object have?" and
"where is attribute `a` relative to the start of the object?"

<!-- another invariant: compare by ID... -->

Just looking at the diagram, it may seem silly: after all, we have exchanged
one hash table for another. There's one big difference, though, which is that
in the shape model, we don't *have* to always use the hash table. If we
remember the offset since the last time we looked it up in the shape, we can
re-use the offset. This mostly appears in an interpreter feature called an
inline cache.

This brings us to **the second bit**: if we have the offset into the object, we
can read right off of it. No need to go through another region of memory and
hash a string. We have the object in hand. Load.

...At least, until the map *changes*. Remember, these languages encourage this
flexibility. An object could gain or lose an attribute at any time. What then?
Ideally we have a very fast check that can tell us if the object's in-memory
layout changed.

This brings us to **the third bit**. We need to be able to rely on the map to
be immutable, up-to-date, and a stable identifier for a given in-memory layout.
This way we can do a very quick check ("is the map the same as last read?")
before a load.

This means that when we add a new attribute to an object, we need to give it a
new map. Maps are actually *trees* (tries?) and are allocated like a persistent
data structure. Every new object starts with an empty map. As the program adds
attributes, it follows the edges in the tree to find or create its new map.

<figure>
<object class="invert" data="/assets/img/maptransition.png"></object>
<figcaption>Image from CF Bolz-Tereick, CC BY 3.0.</figcaption>
</figure>

I recommend reading [the section on
maps](https://aosabook.org/en/500L/a-simple-object-model.html#instance-optimization)
because it goes into a little more detail.

Keeping all of that in mind, let's take a look at CF's Python implementation of
objects-with-maps and also of maps themselves:

```python
class Instance(Instance):
    def __init__(self, cls: "Class"):
        super().__init__(cls)
        self.map = EMPTY_MAP
        self.storage = []

    def _read_dict(self, fieldname):
        index = self.map.get_index(fieldname)
        if index == -1:
            return MISSING
        return self.storage[index]

    def _write_dict(self, fieldname, value):
        index = self.map.get_index(fieldname)
        if index != -1:
            self.storage[index] = value
        else:
            new_map = self.map.next_map(fieldname)
            self.storage.append(value)
            self.map = new_map

class Map:
    def __init__(self, attrs):
        self.attrs = attrs
        self.next_maps = {}

    def get_index(self, fieldname):
        return self.attrs.get(fieldname, -1)

    def next_map(self, fieldname):
        assert fieldname not in self.attrs
        if fieldname in self.next_maps:
            return self.next_maps[fieldname]
        attrs = self.attrs.copy()
        attrs[fieldname] = len(attrs)
        result = self.next_maps[fieldname] = Map(attrs)
        return result

EMPTY_MAP = Map({})
```

You may notice that the storage is not in-line, as I had originally promised.
This is because we don't have that kind of object layout control in Python.
Attribute storage, when implemented in a language such as C++ or Rust, is
typically in-line with the object header.

Ignoring that, though, we can see that the common case still seems to read and
write attributes using string names. What gives? Well, let's use the index
storage as intended in an *inline cache*.

## Inline caches

Let's start by looking at the string attribute API. We'll make a point class and
read the `x` attribute. We'll keep the attribute name constant since this is
generally fixed in the program: `obj.x` normally looks like some bytecode
`LOAD_ATTRIBUTE, "x"` where the name `x` is embedded in the bytecode somehow.

This is important for the inline cache: varying the receiver is one thing but
varying the name being looked up adds a lot more complexity. It's doable but
irritating.

```python
Point = Class(name="Point", base_class=OBJECT, fields={}, metaclass=TYPE)
P = Instance(Point)
P.write_attr("x", 5)
P.write_attr("y", 6)

# String API :(
P.read_attr("x")
# Always looking up index :(
P.read_attr("x")
# Every time :(
P.read_attr("x")
```

Then we will make a memory/speed trade-off. We will allocate space for the
aforementioned *expected map* and the *cached index* mentioned earlier. This
lets us do a really fast check followed by an indexed load in the (hopefully)
common case.

The logic is as follows:

* If we have a matching map, great! Load from storage.
* If we don't, look up the value and index by string name.
* And then cache the map / index combo for next time.

If the attribute is missing, we don't (for now?) cache the negative lookup.
Hopefully this case is rare.

```python
class InlineCache:
    def __init__(self):
        self.map = None
        self.idx = None

    def read(self, obj, fieldname):
        obj_map = obj.map
        if obj_map is self.map:
            return obj.storage[self.idx]
        index = obj_map.get_index(fieldname)
        if index == -1:
            return MISSING
        self.map = obj_map
        self.idx = index
        return obj.storage[index]

IC = InlineCache()
# Looked up by string :(
IC.read(P, "x")
# Map hit! Looked up by index! :)
IC.read(P, "x")
# Map hit! Looked up by index! :)
IC.read(P, "x")
```

You may note that I used `is` (pointer equality) instead of `==`
(user-defined/deep equality). This is intentional! Because we are guaranteeing
the maps are immutable, we don't need to check equality on the fields of the
map; just its pointer/numeric ID.

This `InlineCache` class is mostly just a "sketch" of what an inline cache
does. It is an "interpreted IC". This shape, if you make sure the fast path is
inlined and the slow path is outlined, can help your interpreter a lot. It does
definitely improve performance---I recommend benchmarking a silly little
attribute lookup function---but most runtimes with inline caches also come with
JIT compilers. These compilers try to remove the "interpreted"-ness of the
inline cache implementation. See for example:

* [Slava's post](https://mrale.ph/blog/2015-01-11-whats-up-with-monomorphism.html) that
  explains with IR sketches and JS-like pseudocode
  * also [this older one](https://mrale.ph/blog/2012-06-03-explaining-js-vms-in-js-inline-caches.html)
    which is similar
* [Caio's
post](https://caiolima.github.io/jsc/2020/03/12/jsc-inline-cache.html) that
  explains what this looks like in JavaScriptCore with x86 asm
* [Matthew's post](https://www.mgaudet.ca/technical/2018/6/5/an-inline-cache-isnt-just-a-cache)
  that explains what this looks like in pseudo-asm for speculative numeric addition
  * mostly runtimes don't do self-modifying asm ICs like this because of W^X
    protection and also you need to flush the icache

Another thing to notice is that **order matters**. Adding attributes in different
orders (e.g. `a` then `b` vs `b` then `a`) will result in different shapes.
Another frequent cause of this is conditionally storing attributes in
initialization (e.g. `if cond: self.a = 1`).

This shape and cache behavior is not really visible to the application
programmer except in performance regressions[^attribute-order], where either
your inline caches will frequently miss or you will not have optimal
(monomorphic) caches. This might not sound scary but the difference between a
cache hit and a cache miss in a hot path could be 2x performance or more.

[^attribute-order]: See [this Rails
    PR](https://github.com/rails/rails/pull/57020) by Hartley to make shapes
    more monomorphic. It was initially filed because ZJIT was not capable of
    handling shape polymorphic reads, but it helps other JITs stay monomorphic
    too. Also in this vein is [my PR to
    optcarrot](https://github.com/mame/optcarrot/pull/59).

    ZJIT now does much better with polymorphic shapes but this kind of stuff is
    generally important across languages and runtimes. Enough for people to do
    [large bodies](/assets/img/optimization-coaching.pdf) (PDF) [of
    research](/assets/img/optimization-coaching-js.pdf) (PDF) about
    intelligibly surfacing this kind of information to the application
    programmer.

Also, **quantity matters**. VM implementors measured and determined that most
objects are small (<20 ish fields). You may find that putting too many fields
on an object leads to it getting penalized and put back in "hash table mode".
Ruby, for example, has some fixed number of in-object instance variables
(attributes) before its shape becomes "complex".

Last, notice that this cache is not specific to this object; any number of
`Point` instances can get a cache hit. Actually, looking at it, an object need
not even be a `Point`...

## Class vs shape

What does it mean that two objects with different classes can have the same
shape?

```python
Foo = Class(name="Foo", base_class=OBJECT, fields={}, metaclass=TYPE)
F = Instance(Foo)
F.write_attr("x", 1)
F.write_attr("y", 2)

Bar = Class(name="Bar", base_class=OBJECT, fields={}, metaclass=TYPE)
B = Instance(Bar)
B.write_attr("x", 3)
B.write_attr("y", 4)

# Same shape/map!
assert F.map is B.map
# But different class!
assert F.cls != B.cls
```

For one, it means that the inline cache may have a higher hit rate. Instances
of subclasses that share code, for example, may still count as *monomorphic* in
the IC.

It also means that you can't use the shape as a proxy for knowing about the
class. If you have something like `attr_reader` in Ruby, where the VM generates
a synthetic method just for looking up an instance variable, you need to do two
things:

1. resolve the method based on the *object's class* to find out that it's `attr_reader`
2. resolve the instance variable based on the *object's shape*

This two-stage lookup adds both complexity and run-time overhead. If we
specialized maps so that having a different class implied having a different
map, we would be able to specialize that two-stage dispatch with just one field
load.

This is kind of an interesting idea: what else can we specialize the map on?

## Storage strategies

Perhaps unsurprisingly, CF Bolz-Tereick reappears with Lukas Diekmann and
Laurence Tratt in [Storage Strategies for Collections in Dynamically Typed
Languages](https://dl.acm.org/doi/pdf/10.1145/2544173.2509531) (PDF, 2013).

The main idea is that some facts about objects (properties, attributes, har
har) are both:

* worth splitting/monomorphizing on (there is some kind of "payoff" for the
  increase in branches and code size)
* similarly sensitive to code location (int lists allocated here, float lists
  over there)

Their treatment is for collections (for example, optimizing the common cases of
an empty list, or a list of small integers, ...). In addition to giving more
information to the optimizer, it helps both reduce memory and reduce pointer
chasing.

This kind of thinking extends beyond collections: do you ever have objects
where an attribute is always a certain type? Maybe your `Point` class always
has floats in it? Consider, then, what I think of as CF's follow-up paper, type
freezing: [Type Freezing: Exploiting Attribute Type Monomorphism in Tracing JIT
Compilers](https://dl.acm.org/doi/pdf/10.1145/3368826.3377907) (PDF, 2020)

This paper changes object maps from `{name: offset}` to `{name: (offset,
type)}` to reduce guards. Then it goes further by even addressing nested
attributes and their types. This nesting extension is mostly motivated by the
meta-circularity of the implementation and would have less impact for a
"normal" runtime.

See also:

* [An Efficient Implementation of SELF, a Dynamically-Typed Object-Oriented Language Based on Prototypes](https://bibliography.selflanguage.org/_static/implementation.pdf) (PDF, 1991)
* [Optimizing Dynamically-Dispatched Calls with Run-Time Type Feedback](https://dl.acm.org/doi/pdf/10.1145/178243.178478) (PDF, 1994)
* [An Object Storage Model for the Truffle Language Implementation Framework](https://dl.acm.org/doi/pdf/10.1145/2647508.2647517) (PDF, 2014)
* [Extending Basic Block Versioning with Typed Object Shapes](https://arxiv.org/pdf/1507.02437) (PDF, 2015)
* [Just-in-Time Data Structures](https://dl.acm.org/doi/pdf/10.1145/2814228.2814231) (PDF, 2015)
* [Efficient and thread-safe objects for dynamically-typed languages](https://dl.acm.org/doi/10.1145/2983990.2984001) (PDF, 2016)
* [Parallelization of dynamic languages: synchronizing built-in collections](https://dl.acm.org/doi/pdf/10.1145/3276478) (PDF, 2018)
* [Profile Guided Offline Optimization of Hidden Class Graphs for JavaScript VMs in Embedded Systems](https://dl.acm.org/doi/pdf/10.1145/3563838.3567678) (PDF, 2022)
* [Optimizations for Dynamic Collections](https://www.jot.fm/issues/issue_2025_01/a1.pdf) (PDF, 2025)

## Once more with ~~feeling~~ C++

I felt like this blog post would not be complete without a manual memory layout
rehashing of CF's article. It's all well and good to write the Python but what
do "real" runtimes look like?

Well, they have compact object headers that contain the shape/map and some
other flag bits. They allocate their attributes inline with an object. And they
probably also have an "overflow" slot for when their initial object sizing
estimates were too small.

So I made a very very small runtime in C++ that allocates objects and adds
attributes and has a shape system. It is probably *riddled* with bugs. But it
sketches the kind of memory layouts that underpin Skybison, V8, JSC, and so on.

[Take a look](https://gist.github.com/tekknolagi/caf8462b75ac8ba7e33f6ade06b3f914).

It doesn't have the overflow slot but it's a decent sketch of the underlying
model.

## And more

I fear I am running out of steam on this blog post but there is more to talk
about. Perhaps I will come back and update this post. Or perhaps you will write
a sequel.

<!--
TODO: shape GC (because of classes?)
-->

## Thank you

Thank you to CF Bolz-Tereick and Nery Campusano for thoughtful feedback on this
post.

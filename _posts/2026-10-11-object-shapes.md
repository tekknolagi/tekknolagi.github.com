---
title: "Objects, attributes, maps, oh my!"
layout: post
---

In many dynamically typed programming languages, objects look like a big ol'
hash table of names mapping to values. The languages encourage this
flexibility. Consider for example Python:

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

This might encourage the VM implementor to actually model objects the way that
they behave: as a class and a hash table:

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

* it uses a hash table for a very common operation
* it has an indirection: object attributes are accessed frequently and should
  therefore be stored in-line on the object instead of another memory access
  (and potentially cache miss) away

<figure>
<object class="svg" type="image/svg+xml" data="/assets/img/hashtable-memory.excalidraw.svg"></object>
<figcaption>This is a pretty good case where the hash table doesn't have its own internal
fragmentation.</figcaption>
</figure>

So runtime implementors tend to implement this thing called maps, or layouts,
or shapes, or hidden classes, or structures, or probably a secret sixth name I
don't know about, which optimizes reading, writing, and storage of attributes.

But what does this code look like? A lot of big production runtimes have pretty
hairy code with domain-specific edge cases so they may not help get a handle on
the basics. And a lot of blog posts, while excellent high-level overviews, are
lacking in implementation. So here we go once more, blogging where everyone
else has already blogged before. With any luck, you should be able to write a
page of code and hold the whole thing in your head at once.

## 500 lines or less

I decided to give try once more to give CF Bolz-Tereick's [A Simple Object
Model](https://aosabook.org/en/500L/a-simple-object-model.html) the thorough
read it deserves. In the past, I tried a couple times and bounced off of it
because it models with reasonable fideltiy something close to Python and the
Python object model is *really hairy*.

But did I read it thoroughly this time? Well, no. This time I allowed myself to
skim the class- and method-related parts and instead focus on the instance
attribute and map parts. Inside CF's post is the core of a very clean map
system and I wanted to experiment with that, so I extracted it out.

CF's post starts off very similar to our own sketch, modeling objects as bags
unordered bags of attributes. It also fills in some of the TODO stuff I left
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

# Use it like this (test code taken from CF's post):

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
implementation for `_read_dict` and `_write_dict` for better performance. A
`Class` is an object too and therefore should also be able to read and write
attributes, but we are for some reason not going to give it the same
performance.

<!-- A map by any other name would be as fleet -->

TODO: indirection (C++ object allocator)

TODO: storage strategies

TODO: Rails column optimization

TODO: shape GC (because of classes?)

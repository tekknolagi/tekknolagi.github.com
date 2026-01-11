---
title: "Value numbering"
layout: post
---

Or, *But my values. They already have numbers!*

Static single assignment (SSA) gives names to values: every expression has a
name, and each name corresponds to exactly one expression. It transforms
programs like this:

```
x = 0
x = x + 1
x = x + 1
```

into

```
v0 = 0
v1 = v0 + 1
v2 = v1 + 1
```

It's great because it makes clear the differences between the two `x + 1`
expressions. Though they textually look similar, they compute different values.
The first computes 1 and the second computes 2. In this example, it is not
possible to substitute in a variable and re-use the value of `x + 1`, because
the `x`s are different.

Now let us move to the land of JIT compilers and SSA.

## JIT compilers and SSA

Frequently in a compiler, especially a compiler for a dynamic language, we may
see a program come into the optimizer looking like this:

```
v0 = ...
v1 = 123
SetIvar v0, "a", v1    # v0.a = 123, ish
v3 = 456
SetIvar v0, "b", v3    # v1.b = 456, ish
```

In this example, the instruction `SetIvar` represents an generic attribute
write, a property write, an instance variable write---whatever you call it---an
operation that behaves like storing a value into a hashmap on an object.

Because such operations are frequent and hashmap operations execute quite a few
instructions, dynamic language runtimes often implement these "hashmaps" with
shapes/layouts/maps/hidden classes. This means each object contains a pointer
to a description of its memory layout. This layout pointer (which I will call
"shape" for the duration of this post) can change over time as attributes are
added or deleted.

Consider an annotated version of the above IR snippet:

```
v0 = ...
# state 0
v1 = 123
SetIvar v0, "a", v1    # v0.a = 123, ish
# state 1
v3 = 456
SetIvar v0, "b", v3    # v1.b = 456, ish
# state 2
```

<!--
At state 0, the object has some unknown shape. At state 1, it has a new shape
that has the `a` attribute. At state 2, it has a new shape that has both the
`a` and `b` fields.
-->

These `SetIvar` operations are very generic, so we want to optimize them. We
have observed in the interpreter that the object coming into the first
`SetIvar` frequently enters with the shape ID `Shape700` (for example). The
compiler looks, and `Shape700` is empty. In order to add a new attribute to the
object, we have to

https://aosabook.org/en/500L/a-simple-object-model.html
https://mathiasbynens.be/notes/shapes-ics
https://poddarayush.com/posts/object-shapes-improve-ruby-code-performance/
https://chrisseaton.com/truffleruby/rubykaigi21/
https://chromium.googlesource.com/external/github.com/v8/v8.wiki/+/60dc23b22b18adc6a8902bd9693e386a3748040a/Design-Elements.md#dynamic-machine-code-generation

https://www.cs.cornell.edu/courses/cs6120/2019fa/blog/global-value-numbering/

---
title: Representing type lattices compactly
layout: post
co_authors: Brett Simmers
---

The Cinder JIT compiler does some cool stuff with how they represent types so
I'm going to share it with you here. The core of it is thinking about types as
*sets* (lattices, even), and picking a compact representation. Compilers will
create and manipulate types with abandon, so all operations have to be fast.

We'll start from first principles and build our way up to roughly what Cinder
has (and we could go further from there).

## Types as sets

Types, as a concept, name sets of objects---sets of instances. Some types (say,
`int8`) have finite members. There are only ever 256 potential values for an
8-bit integer. Some times, (say, `list`) are infinite. Given enough storage,
one could keep generating bigger and bigger instances. Since it's not possible
to store all elements of a set, we refer to types by a name.

This reduces precision. Maybe there is some bizarre case where you know that
an object could be one of a thousand different possible `list`s, so giving up
and saying "it's a `list`" loses information. But it also saves a bunch of
space and analysis time, because now we're dealing with very small labels.
Let's start off by giving a couple built-in types names and calling it a day.

## Starting simple

A reasonable first way to give types names is with an enum:

```c
enum {
    Int,
    List,
    String,
    Object,  // catch-all
};
```

Not bad. We can represent some built-in types and we have a catch-all case to
use when we don't know what type something is or the type doesn't fit neatly
into our enum (`Object`), which captures the `object` type and all of its
subclasses.

Using this enum, we can assign types to variables (for the purposes of this
post, SSA values) and use those types to optimize. For example, we might see
the following pseudo-Python snippet:

```python
a = [...]
return len(a)
```

If we know that the type of `a` is `List`, we can optimize the call to
`len`[^redefining-len] to a direct call to the `list` class's `__len__` method,
or---even better, since we know about CPython's runtime details---read the
`ob_size` field directly off the object.

[^redefining-len]: Pretend for now that we can't redefine or shadow `len`
    because that trickiness is unrelated to this post.

That's great! It will catch a bunch of real-world cases and speed up code.
Unfortunately, it gives up a little too easily. Consider some silly-ish code
that either assigns a `str` or a `list` depending on some condition:

```python
def foo(cond):
    if cond:
        a = "..."
    else:
        a = [...]
    return len(a)
```

Because `a` could have either type, we have to union the type information we
know into a more general type. Since we don't have  `ListOrString` case in our
enum (nor should we), we have to pick something more general: `Object`.

That's a bummer, because we as runtime implementors know that both `str` and
`list` have `__len__` methods that just read the `ob_size` field. Now that we
have lost so much information in the type union, we have to do a very generic
call to the `len` function.

To get around this, we can turn the straightforward enum into a *bitset*.

## Bitsets

Bitsets encode set information by assigning each bit in some number (say, a
64-bit word) a value. If that bit is 1, the value is in the set. Otherwise, it
isn't.

Here's the enum from earlier, remade as a bitset:

```c
enum {
    Int    = 1 << 0,  // 0b001
    List   = 1 << 1,  // 0b010
    String = 1 << 2,  // 0b100
    Object = 7,       // 0b111; catch-all
};
```

We have three elements: `Int`, `List`, and `String`. Since we have a bit for
each object and they can be addressed individually, we can very
straightforwardly encode the `ListOrString` type using bitwise arithmetic:

```c
List | String  // 0b110
```

It doesn't even need to be a basic type in our enum. We can construct it using
our built-in building blocks.

We have also set our unknown/catch-all/`Object` type as the value with *all*
bits set, so that if we *or* together all of our known types
(`Int|List|String`), the unknown-ness falls out naturally.

...but what does the unnamed 0 value represent? If we are thinking in terms of
sets still (which we should be), then 0 means no bits set, which means it
represents 0 elements, which means it's the empty set!

We're starting to approach a very useful mathematical structure called a
*semilattice*.

## Semilattices

I'm not going to quote you any formal definitions because I'm not very mathy,
but a semilattice is a partial (not total) order of a set with a *join*
(*union*) operation. It has a top element and a bottom element.

For our limited type data structure, our partial order is determined by set
membership and our *join* is set union. Top represents "all possible objects"
and bottom represents "no objects":

* bottom is less than every other set, because it has no elements
* int, list, and string are not ordered amongst themselves because none of them
  is a superset of any of the others
* top (object) is greater than every set, because it has all elements

Check out this handy diagram of our original enum-based type lattice:

<!--
digraph G {
    rankdir="BT";
    int -> any;
    str -> any;
    list -> any;

    any [label="Object/Top/Any"];
    int [label="Int"];
    str [label="String"];
    list[label="List"];
}
-->

<figure>
<svg width="222pt" height="116pt" viewBox="0.00 0.00 222.00 116.00" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">
<g id="graph0" class="graph" transform="scale(1 1) rotate(0) translate(4 112)">
<title>G</title>
<polygon fill="white" stroke="none" points="-4,4 -4,-112 218,-112 218,4 -4,4"/>
<!-- int -->
<g id="node1" class="node">
<title>int</title>
<ellipse fill="none" stroke="black" cx="27" cy="-18" rx="27" ry="18"/>
<text text-anchor="middle" x="27" y="-13.8" font-family="Times,serif" font-size="14.00">Int</text>
</g>
<!-- any -->
<g id="node2" class="node">
<title>any</title>
<ellipse fill="none" stroke="black" cx="107" cy="-90" rx="74.33" ry="18"/>
<text text-anchor="middle" x="107" y="-85.8" font-family="Times,serif" font-size="14.00">Object/Top/Any</text>
</g>
<!-- int&#45;&gt;any -->
<g id="edge1" class="edge">
<title>int-&gt;any</title>
<path fill="none" stroke="black" d="M42.81,-32.83C53.21,-41.94 67.12,-54.1 79.33,-64.79"/>
<polygon fill="black" stroke="black" points="76.87,-67.29 86.7,-71.24 81.48,-62.02 76.87,-67.29"/>
</g>
<!-- str -->
<g id="node3" class="node">
<title>str</title>
<ellipse fill="none" stroke="black" cx="107" cy="-18" rx="34.65" ry="18"/>
<text text-anchor="middle" x="107" y="-13.8" font-family="Times,serif" font-size="14.00">String</text>
</g>
<!-- str&#45;&gt;any -->
<g id="edge2" class="edge">
<title>str-&gt;any</title>
<path fill="none" stroke="black" d="M107,-36.3C107,-43.59 107,-52.27 107,-60.46"/>
<polygon fill="black" stroke="black" points="103.5,-60.38 107,-70.38 110.5,-60.38 103.5,-60.38"/>
</g>
<!-- list -->
<g id="node4" class="node">
<title>list</title>
<ellipse fill="none" stroke="black" cx="187" cy="-18" rx="27" ry="18"/>
<text text-anchor="middle" x="187" y="-13.8" font-family="Times,serif" font-size="14.00">List</text>
</g>
<!-- list&#45;&gt;any -->
<g id="edge3" class="edge">
<title>list-&gt;any</title>
<path fill="none" stroke="black" d="M171.19,-32.83C160.79,-41.94 146.88,-54.1 134.67,-64.79"/>
<polygon fill="black" stroke="black" points="132.52,-62.02 127.3,-71.24 137.13,-67.29 132.52,-62.02"/>
</g>
</g>
</svg>
</figure>

The arrows indicate that applying a *join* will only ever move us upward along
an arrow.  For example, `join(Int, String)` is `Top` (we don't have any
finer-grained way to represent all ints and all strings).

Compare this with, for example, what we can represent with our bitset type
representation:

<!--
digraph G {
    rankdir="BT";
    empty -> list;
    empty -> str;
    empty -> int;

    int -> int_str;
    int -> int_list;
    str -> int_str;
    str -> str_list;
    list -> int_list;
    list -> str_list;

    int_str -> any;
    int_list -> any;
    str_list -> any;

    empty [label="Bottom/Empty"];
    int_str [label="Int|String"];
    int_list [label="Int|List"];
    str_list [label="String|List"];
    any [label="Object/Top/Any"];
    int [label="Int"];
    str [label="String"];
    list[label="List"];
}
-->

<figure>
<svg width="386pt" height="260pt" viewBox="0.00 0.00 385.53 260.00" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">
<g id="graph0" class="graph" transform="scale(1 1) rotate(0) translate(4 256)">
<title>G</title>
<polygon fill="white" stroke="none" points="-4,4 -4,-256 381.53,-256 381.53,4 -4,4"/>
<!-- empty -->
<g id="node1" class="node">
<title>empty</title>
<ellipse fill="none" stroke="black" cx="199.92" cy="-18" rx="68.45" ry="18"/>
<text text-anchor="middle" x="199.92" y="-13.8" font-family="Times,serif" font-size="14.00">Bottom/Empty</text>
</g>
<!-- list -->
<g id="node2" class="node">
<title>list</title>
<ellipse fill="none" stroke="black" cx="279.92" cy="-90" rx="27" ry="18"/>
<text text-anchor="middle" x="279.92" y="-85.8" font-family="Times,serif" font-size="14.00">List</text>
</g>
<!-- empty&#45;&gt;list -->
<g id="edge1" class="edge">
<title>empty-&gt;list</title>
<path fill="none" stroke="black" d="M218.88,-35.59C229.85,-45.19 243.78,-57.37 255.56,-67.68"/>
<polygon fill="black" stroke="black" points="253.09,-70.17 262.92,-74.13 257.7,-64.91 253.09,-70.17"/>
</g>
<!-- str -->
<g id="node3" class="node">
<title>str</title>
<ellipse fill="none" stroke="black" cx="199.92" cy="-90" rx="34.65" ry="18"/>
<text text-anchor="middle" x="199.92" y="-85.8" font-family="Times,serif" font-size="14.00">String</text>
</g>
<!-- empty&#45;&gt;str -->
<g id="edge2" class="edge">
<title>empty-&gt;str</title>
<path fill="none" stroke="black" d="M199.92,-36.3C199.92,-43.59 199.92,-52.27 199.92,-60.46"/>
<polygon fill="black" stroke="black" points="196.42,-60.38 199.92,-70.38 203.42,-60.38 196.42,-60.38"/>
</g>
<!-- int -->
<g id="node4" class="node">
<title>int</title>
<ellipse fill="none" stroke="black" cx="108.92" cy="-90" rx="38.38" ry="18"/>
<text text-anchor="middle" x="108.92" y="-85.8" font-family="Times,serif" font-size="14.00">Int</text>
</g>
<!-- empty&#45;&gt;int -->
<g id="edge3" class="edge">
<title>empty-&gt;int</title>
<path fill="none" stroke="black" d="M178.82,-35.24C166.45,-44.75 150.66,-56.9 137.22,-67.23"/>
<polygon fill="black" stroke="black" points="135.35,-64.25 129.56,-73.12 139.62,-69.8 135.35,-64.25"/>
</g>
<!-- int_list -->
<g id="node6" class="node">
<title>int_list</title>
<ellipse fill="none" stroke="black" cx="200.92" cy="-162" rx="55.34" ry="18"/>
<text text-anchor="middle" x="200.92" y="-157.8" font-family="Times,serif" font-size="14.00">Int|List</text>
</g>
<!-- list&#45;&gt;int_list -->
<g id="edge8" class="edge">
<title>list-&gt;int_list</title>
<path fill="none" stroke="black" d="M264.32,-104.83C253.89,-114.06 239.92,-126.45 227.74,-137.24"/>
<polygon fill="black" stroke="black" points="225.57,-134.49 220.4,-143.74 230.21,-139.73 225.57,-134.49"/>
</g>
<!-- str_list -->
<g id="node7" class="node">
<title>str_list</title>
<ellipse fill="none" stroke="black" cx="325.92" cy="-162" rx="51.6" ry="18"/>
<text text-anchor="middle" x="325.92" y="-157.8" font-family="Times,serif" font-size="14.00">String|List</text>
</g>
<!-- list&#45;&gt;str_list -->
<g id="edge9" class="edge">
<title>list-&gt;str_list</title>
<path fill="none" stroke="black" d="M290.36,-106.88C295.79,-115.15 302.57,-125.46 308.75,-134.86"/>
<polygon fill="black" stroke="black" points="305.64,-136.5 314.05,-142.94 311.49,-132.66 305.64,-136.5"/>
</g>
<!-- int_str -->
<g id="node5" class="node">
<title>int_str</title>
<ellipse fill="none" stroke="black" cx="63.92" cy="-162" rx="63.92" ry="18"/>
<text text-anchor="middle" x="63.92" y="-157.8" font-family="Times,serif" font-size="14.00">Int|String</text>
</g>
<!-- str&#45;&gt;int_str -->
<g id="edge6" class="edge">
<title>str-&gt;int_str</title>
<path fill="none" stroke="black" d="M175.92,-103.35C155.86,-113.68 126.85,-128.61 103.38,-140.69"/>
<polygon fill="black" stroke="black" points="102,-137.46 94.71,-145.15 105.21,-143.69 102,-137.46"/>
</g>
<!-- str&#45;&gt;str_list -->
<g id="edge7" class="edge">
<title>str-&gt;str_list</title>
<path fill="none" stroke="black" d="M223.03,-103.84C241.59,-114.15 268,-128.82 289.42,-140.72"/>
<polygon fill="black" stroke="black" points="287.71,-143.77 298.15,-145.57 291.11,-137.65 287.71,-143.77"/>
</g>
<!-- int&#45;&gt;int_str -->
<g id="edge4" class="edge">
<title>int-&gt;int_str</title>
<path fill="none" stroke="black" d="M98.26,-107.59C93.07,-115.66 86.7,-125.57 80.87,-134.65"/>
<polygon fill="black" stroke="black" points="78.11,-132.47 75.64,-142.77 83.99,-136.25 78.11,-132.47"/>
</g>
<!-- int&#45;&gt;int_list -->
<g id="edge5" class="edge">
<title>int-&gt;int_list</title>
<path fill="none" stroke="black" d="M128.43,-105.85C140.81,-115.26 157.06,-127.63 171.04,-138.27"/>
<polygon fill="black" stroke="black" points="168.56,-140.77 178.63,-144.04 172.79,-135.2 168.56,-140.77"/>
</g>
<!-- any -->
<g id="node8" class="node">
<title>any</title>
<ellipse fill="none" stroke="black" cx="200.92" cy="-234" rx="74.33" ry="18"/>
<text text-anchor="middle" x="200.92" y="-229.8" font-family="Times,serif" font-size="14.00">Object/Top/Any</text>
</g>
<!-- int_str&#45;&gt;any -->
<g id="edge10" class="edge">
<title>int_str-&gt;any</title>
<path fill="none" stroke="black" d="M93.99,-178.36C113.35,-188.25 138.76,-201.24 159.96,-212.07"/>
<polygon fill="black" stroke="black" points="158.13,-215.07 168.63,-216.5 161.32,-208.83 158.13,-215.07"/>
</g>
<!-- int_list&#45;&gt;any -->
<g id="edge11" class="edge">
<title>int_list-&gt;any</title>
<path fill="none" stroke="black" d="M200.92,-180.3C200.92,-187.59 200.92,-196.27 200.92,-204.46"/>
<polygon fill="black" stroke="black" points="197.42,-204.38 200.92,-214.38 204.42,-204.38 197.42,-204.38"/>
</g>
<!-- str_list&#45;&gt;any -->
<g id="edge12" class="edge">
<title>str_list-&gt;any</title>
<path fill="none" stroke="black" d="M299.41,-177.85C281.96,-187.62 258.83,-200.57 239.36,-211.47"/>
<polygon fill="black" stroke="black" points="237.91,-208.28 230.89,-216.22 241.33,-214.39 237.91,-208.28"/>
</g>
</g>
</svg>
</figure>

See how we have two new layers! We have a Bottom (empty) type now. For example,
`join(Bottom, Bottom)` is `Bottom` (it remains empty), and `join(Bottom, List)`
is `List` (no new elements).

We can also address the set of all ints and all strings with `Int|String`,
which is ordered above each of the invidual int and string sets.

As I alluded to earlier, we can conveniently represent a bottom element in our
bitset type:

```c++
enum Type {
    Bottom = 0,       // 0b000
    Int    = 1 << 0,  // 0b001
    List   = 1 << 1,  // 0b010
    String = 1 << 2,  // 0b100
    Top    = 7,       // 0b111
};


enum Type join(enum Type left, enum Type right) {
    return left | right;
}

// Ask if `left` is a subtype of `right`
bool is_subtype(enum Type left, enum Type right) {
    return (left & right) == left;
}
```

<!-- TODO(max): Maybe talk about intersection from `if isinstance` refinement,
GuardType refinement, or could_be -->

This type representation is neat, but we can go further. Sometimes, you know
more than just the *type* of an object: you know exactly what object it is.
We'll introduce what Cinder calls *specialization*.

## Specialization

In addition to our existing type lattice, Cinder keeps a second lattice called
a specialization. Its job is to keep track of what specific object an SSA value
is. For example, if we know that `a = 5`, its type would be `Int` and its
specialization would be `5`.

The specialization lattice is much simpler:

```c
struct Spec {
    enum {
        SpecTop,
        SpecInt,
        SpecBottom,
    } spec_kind;
    int value;
};
```

There's an internal enum that says what we know about the specialization and an
optional value. There are a couple of cases:

* if we have `SpecTop`, we don't have any information about the specialization
* if we have `SpecInt`, we know exactly the integer value, and the `value`
  field is valid (initialized, readable)
* if we have `SpecBottom`, we know that the spec contains no elements (and
  therefore also must have a type of `Bottom`)

Here's what the diagram looks like:

<!--
digraph G {
    rankdir="BT";
    bottom -> int_spec;
    int_spec -> top;
    bottom [label="Bottom"];
    int_spec [label="Int[N]"];
    top [label="Top"];
}
-->

<figure>
<svg width="88pt" height="188pt" viewBox="0.00 0.00 88.02 188.00" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">
<g id="graph0" class="graph" transform="scale(1 1) rotate(0) translate(4 184)">
<title>G</title>
<polygon fill="white" stroke="none" points="-4,4 -4,-184 84.02,-184 84.02,4 -4,4"/>
<!-- bottom -->
<g id="node1" class="node">
<title>bottom</title>
<ellipse fill="none" stroke="black" cx="40.01" cy="-18" rx="40.01" ry="18"/>
<text text-anchor="middle" x="40.01" y="-13.8" font-family="Times,serif" font-size="14.00">Bottom</text>
</g>
<!-- int_spec -->
<g id="node2" class="node">
<title>int_spec</title>
<ellipse fill="none" stroke="black" cx="40.01" cy="-90" rx="35.17" ry="18"/>
<text text-anchor="middle" x="40.01" y="-85.8" font-family="Times,serif" font-size="14.00">Int[N]</text>
</g>
<!-- bottom&#45;&gt;int_spec -->
<g id="edge1" class="edge">
<title>bottom-&gt;int_spec</title>
<path fill="none" stroke="black" d="M40.01,-36.3C40.01,-43.59 40.01,-52.27 40.01,-60.46"/>
<polygon fill="black" stroke="black" points="36.51,-60.38 40.01,-70.38 43.51,-60.38 36.51,-60.38"/>
</g>
<!-- top -->
<g id="node3" class="node">
<title>top</title>
<ellipse fill="none" stroke="black" cx="40.01" cy="-162" rx="27" ry="18"/>
<text text-anchor="middle" x="40.01" y="-157.8" font-family="Times,serif" font-size="14.00">Top</text>
</g>
<!-- int_spec&#45;&gt;top -->
<g id="edge2" class="edge">
<title>int_spec-&gt;top</title>
<path fill="none" stroke="black" d="M40.01,-108.3C40.01,-115.59 40.01,-124.27 40.01,-132.46"/>
<polygon fill="black" stroke="black" points="36.51,-132.38 40.01,-142.38 43.51,-132.38 36.51,-132.38"/>
</g>
</g>
</svg>
</figure>

Where *N* represents some integer stored in the `value` field.

This complicates things a bit. Let's put both the type bits and the
specialization together in one structure and admire:

```c++
struct Type {
    enum TypeBits {
        Bottom = 0,       // 0b000
        Int    = 1 << 0,  // 0b001
        List   = 1 << 1,  // 0b010
        String = 1 << 2,  // 0b100
        Top    = 7,       // 0b111
    } type;
    // If you are feeling clever, you can also steal some `type` bits for the
    // `spec_kind` instead of putting it in a separate field.
    struct Spec {
        enum {
            SpecTop,
            SpecInt,
            SpecBottom,
        } spec_kind;
        int value;
    } spec;
};

struct Type TTop = (struct Type) {
    .type = Top,
    .spec = (struct Spec) { .spec_kind = SpecTop },
};
struct Type TInt = (struct Type) {
    .type = Int,
    .spec = (struct Spec) { .spec_kind = SpecTop },
};
// Invariant: .type == Bottom if and only if .spec_kind == SpecBottom.
struct Type TBottom = (struct Type) {
    .type = Bottom,
    .spec = (struct Spec) { .spec_kind = SpecBottom },
};
```

That's very nice and more precise, but now our `join` and `is_subtype`
operators don't make a whole lot of sense. We can't just use bitwise operations
any more. We have to also do the lattice operations on the `Spec` field:

```c++
struct Type join(struct Type left, struct Type right) {
    struct Type result;
    result.type = left.type | right.type;
    result.spec = spec_join(left.spec, right.spec);
    return result;
}

// Ask if `left` is a subtype of `right`
bool is_subtype(struct Type left, struct Type right) {
    return (left & right) == left && spec_is_subtype(left.spec, right.spec);
}
```

If we decompose the problem that way, we can write some lattice operations for
`Spec`. Let's start with `is_subtype`:

```c++
// Ask if `left` is a subtype of `right`
bool spec_is_subtype(struct Spec left, struct Spec right) {
    if (right.spec_kind == SpecTop || left.spec_kind == SpecBottom) {
        // Top is a supertype of everything and Bottom is a subtype of
        // everything
        return true;
    }
    // left is now either SpecInt or SpecTop
    // right is now either SpecInt or SpecBottom
    // The only way left could be a subtype of right is if they are both
    // SpecInt and the value is the same
    if (left.spec_kind == SpecInt &&
        right.spec_kind == SpecInt &&
        left.value == right.value) {
        return true;
    }
    return false;
}
```

That takes a bit of time to internalize, so please read over it a couple of
times and work out the cases by hand. It's really useful for implementing
`spec_join`:

```c
struct Spec spec_join(struct Spec left, struct Spec right) {
    if (spec_is_subtype(left, right)) { return right; }
    if (spec_is_subtype(right, left)) { return left; }
    // We know that neither left nor right is either SpecTop or SpecBottom
    // because that would have been covered in one of the subtype cases, so
    // we're join-ing two SpecInts. That's SpecTop.
    struct Spec result;
    result.spec_kind = SpecTop;
    return result;
}
```

It's a couple more instructions than a single bitwise operation and a compare,
but it's still compact and fast.

Let's talk about some problems you might run into while using this API.

## Bottom API

A common mistake when handling `Bottom` is treating it as an error, or assuming
it will never show up in your program. To expand on its brief introduction
above, the two main consequences of `Bottom`'s place at the bottom of the type
lattice are:

1. A value of type `Bottom` can never be *defined*. An instruction with an
   output of type `Bottom` will therefore never define its output. Such an
   instruction might loop infinitely, it might crash your program, or it might
   jump to another location in the program (if your compiler supports
   control-flow instructions that define values).
2. A value of type `Bottom` can never be *used*, so an instruction with an
   input of type `Bottom` is unreachable. This follows from the previous item
   (since you obviously can't use a value you can't define), but is worth
   calling out separately.

These two properties make checking for `Bottom` useful in an unreachable code
elimination pass. Otherwise, though, `Bottom` is just another type, and your
type predicates will generally be cleaner if you design them to correctly
handle `Bottom` without explicitly testing for it.

Most of the time, `Bottom` support happens out naturally:

```c++
if (t <= TLongExact) {
    // generate int-specific code
}
```

In this example, `t == TBottom` will be fine: any code that is generated (if
your compiler doesn't eliminate unreachable code) will never run, so it's
perfectly fine to call a helper function that expects an `int`, or to load a
field of `PyLongObject`, etc.

Sometimes, however, we want to get a concrete value out of a type at
compile-time for an optimization such as constant folding. It may be tempting
to assume that if your type `t` is a strict subtype of `LongExact`, it
represents a specific `int` object:

```c++
if (t < TLongExact) {
    PyLongObject* a_long = t.asPyObject();
    // ... optimize based on a_long
}
```

This code is broken for plenty of cases other than `Bottom` (e.g.,
range-constrained `int` types), but in many compilers, `Bottom` will usually be
the first type that causes a crash or failed assertion here. Rather than
excluding `Bottom` by name, with code like `if (t != TBottom && t <
TLongExact)`, you can handle `Bottom` (and all other types!) correctly by
refining your type predicate to what you *really* mean.

In this case, you want to know if `t` represents exactly one value, so you
might use an API like `t.admitsSingleValue(TLongExact)`. That will correctly
exclude `Bottom`, which represents zero values, but it will also correctly
exclude a type that means "an `int` that is less than 0", which is a strict
subtype of `TLongExact` but doesn't represent a single value.

Now, these type bits are a pain to write down by hand. Instead, it would be
nice to generate them automatically given a class hierarchy. That's what Cinder
does.

## Generating the lattice from a type hierarchy

Here is an abridged sketch of the Cinder script to generate it, since the
Cinder version has a couple more quirks that are not crucial to understanding
the core of this post:

```python
class UnionSpec(NamedTuple):
    name: str
    components: List[str]

# All the leaf types, each of which gets a bit
BASIC_FINAL_TYPES: List[str] = [
    "Bool",
    "NoneType",
    # ...
]

BASIC_BASE_TYPES: List[str] = [
    "Bytes",
    "Dict",
    # ...
]

BASIC_EXACT_TYPES: List[str] = [
    "ObjectExact",
    *[ty + "Exact" for ty in BASIC_BASE_TYPES],
]

BASIC_USER_TYPES: List[str] = [
    "ObjectUser",
    *[ty + "User" for ty in BASIC_BASE_TYPES],
]

BASIC_PYTYPES: List[str] = BASIC_FINAL_TYPES + BASIC_EXACT_TYPES + BASIC_USER_TYPES

# The unions, which have multiple bits set
PYTYPE_UNIONS: List[UnionSpec] = [
    UnionSpec("BuiltinExact", BASIC_FINAL_TYPES + BASIC_EXACT_TYPES),
    *[UnionSpec(ty, [ty + "User", ty + "Exact"]) for ty in BASIC_BASE_TYPES],
    UnionSpec("User", BASIC_USER_TYPES),
    UnionSpec("Object", BASIC_PYTYPES),
    UnionSpec("Top", BASIC_PYTYPES),
    UnionSpec("Bottom", []),
]

def assign_bits() -> Tuple[Dict[str, int], int]:
    """Create the bit patterns for all predefined types: basic types are given
    one bit each, then union types are constructed from the basic types.
    """
    bit_idx = 0
    bits = {}
    for ty in BASIC_PYTYPES:
        bits[ty] = 1 << bit_idx
        bit_idx += 1

    for ty, components, _ in PYTYPE_UNIONS:
        bits[ty] = reduce(operator.or_, [bits[t] for t in components], 0)

    return bits, bit_idx
```

It's a bit of a funky snippet, but the gist is that it creates a bunch of leaf
types such as `DictExact` and `BytesUser` and assigns each a bit. Then, it goes
through all of the unions and bitwise `or`s their component bits together. Last
(not pictured), it prints out a `uint64_t` for each of these.

The output is an X-macro that looks something like this:

```c
// For all types, call X(name, bits)
#define HIR_TYPES(X) \
  X(Array,                         0x00000000001UL)    \
  X(BaseException,                 0x00000801000UL)    \
  X(BaseExceptionExact,            0x00000001000UL)    \
  X(BaseExceptionUser,             0x00000800000UL)    \
  X(Bool,                          0x00000000002UL)    \
  /* ... */                                            \
  X(User,                          0x000ffe00000UL)    \
  X(WaitHandle,                    0x00000000200UL)
```

Check out the [Cinder implementation][generate-type].

[generate-type]: https://github.com/facebookincubator/cinderx/blob/9197ff2a80517304e194ea36b71f973b7daa1bd9/Jit/hir/generate_jit_type_h.py

## Some extras

Cinder also has some other features embedded into the same type bitset:

* Primitive type bits and primitive specializations (`CInt32`, `CDouble`,
  `CBool`, `Null`, ...), which inherently handles nullability
* A lifetime lattice to support [immortal objects][immortal-pep]

[immortal-pep]: https://peps.python.org/pep-0683/

See also the [type.md][type-md] document which goes into detail about Cinder's
type system.

[type-md]: https://github.com/facebookincubator/cinderx/blob/08ec283e5eaf29196d92f8d308aaadc8e82bc0c0/Jit/hir/type.md

There are some features Cinder does not (currently) support but would be cool
to implement:

* Integer ranges
* Known bits / tristate numbers
* Function pointers
* Non-builtin class unions (as a bitset of class IDs or otherwise)

## In other compilers

* HHVM has a similar looking [type.h][hhvm-type] and
  [specialization][hhvm-type-spec]. It's similar looking because both Cinder's
  and HHVM's were written by my friend and former coworker Brett Simmers!
  * See also the [type implementation][hhbbc-type] for the bytecode optimizer
* iv, written by Constellation (JavaScriptCore committer), has a [smaller bitset
  lattice][iv-type]
* V8's Turbofan compiler has [something similar][turbofan-type]
* Simple (the Sea of Nodes compiler)'s [Type.java][simple-type]
* Cliff Click's aa language [Type.java][aa-type]
* HotSpot's C2's [type.hpp][c2-type]
* PyPy's RPython generates [heap effect metadata][pypy-effect] for functions
  that can't be inlined so that the JIT knows what heap addresses they do and
  do not write to. It does not use a bitset (yet?) but is a lattice.

[hhvm-type]: https://github.com/facebook/hhvm/blob/11b663fdfde613d477f38af04db15f7ec1ee9bf3/hphp/runtime/vm/jit/type.h
[hhvm-type-spec]: https://github.com/facebook/hhvm/blob/11b663fdfde613d477f38af04db15f7ec1ee9bf3/hphp/runtime/vm/jit/type-specialization-inl.h
[hhbbc-type]: https://github.com/facebook/hhvm/blob/30babc75b3a0cec4fcbf7823493913cca4323a58/hphp/hhbbc/type-system.h
[iv-type]: https://github.com/Constellation/iv/blob/64c3a9c7c517063f29d90d449180ea8f6f4d946f/iv/lv5/breaker/type.h
[turbofan-type]: https://github.com/v8/v8/blob/30be5d03036d6934e847c733315d527915207e85/src/compiler/turbofan-types.h
[simple-type]: https://github.com/SeaOfNodes/Simple/blob/2370fb29d4538479af9eb94fc666a5ce09fcb492/chapter20/src/main/java/com/seaofnodes/simple/type/Type.java
[aa-type]: https://github.com/cliffclick/aa/blob/e50e4a5881f9a15c1c7f063417eaa7b43c23f1bd/src/main/java/com/cliffc/aa/type/Type.java
[c2-type]: https://github.com/openjdk/jdk/blob/857c53718957283766f6566e5519ab5911cf9f3c/src/hotspot/share/opto/type.hpp
[pypy-effect]: https://github.com/pypy/pypy/blob/c2324b8ac30693d049214bf6a0447d52ebbfc352/rpython/translator/backendopt/writeanalyze.py

## Acknowledgements

Thanks to CF Bolz-Tereick, Cliff Click, and Kai Williams for feedback on this
post.

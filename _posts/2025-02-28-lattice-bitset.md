---
title: Representing type lattices compactly
layout: post
---

The Cinder JIT compiler does some cool stuff with how they represent types so
I'm going to share it with you here. The core of it is thinking about types as
*sets*, and picking a compact set representation.

We'll start from first principles and build our way up to roughly what Cinder
has (and we could go further from there).

## Starting simple

Say you want to represent types in your compiler. A reasonable first way to do
that is with an enum:

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
into our enum (`Object`).

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
    Int    = 1 << 0,
    List   = 1 << 1,
    String = 1 << 2,
    Object = 7,  // 0b111; catch-all
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

## Types as sets

...but what does the 0 value represent? Let's talk a bit about lattices.

## Lattices

Top/Any, Bottom/Empty

## Bottom API

## Specialization

## Generating the lattice

---




The core constraints are:

* We want full unions, not the discriminated unions given to us by `enum`s
* We want types to be very quick to allocate and free, since the compiler will
  create them with abandon
* We want a union/meet operation (all set operations, really) to be fast, since
  they will be used a lot during optimization

I'll be using some Cinder-specific notation for the duration of the blog post,
but it should be easy enough to get used to. The notation looks like this:

* `ATypeExact` means that exact type (the set of all possible instances of that
  exact type)
* `ATypeUser` means that a subclass of that type (the set of all possible
  instances of subclasses of that type)
* `AType` is the union of `ATypeExact` and `ATypeUser`
* `AType[something]` means a *specific* instance of that type
* `AType|AnotherType` means either `AType` or `AnotherType` (the union/meet of
  the two sets)

In fact, we want to be able to union/meet any two types together. That sounds like
the perfect use case for a bitset. Assign the "leaf types" in the hierarchy
one bit each and then unions are sets of 1 bits. It's really fast, too! Bitwise
`&` and `|` are one hardware instruction each and can quickly compute the
information we need. Amazing, right?

> Not only is this slower with an enum approach but it is also more difficult
> to get right. Say you have an `enum { Object, String, Int, SpecificInt(i32) }`.
> There are a lot of corner cases for union, intersection, and subtraction that
> you have to think about that you would otherwise get "for free" using a
> bitset approach.
>
> So you lose your language-given pattern matching feature to check what type
> an instruction is. But that's okay! We almost never want to know *exactly*
> what type an instruction is when optimizing, but instead if it's a subtype of
> the desired type.

It also wants the types to form a *lattice*, so there has to be a way to
represent some element `Top` (could be any value of any type; everything is a
subtype of Top) and some element `Bottom` (has no value, or the instruction
that produces it does not return; Bottom is a subtype of everything). This is
also straightforward: `Top` is represented by setting every bit and `Bottom` by
setting none of the bits.

See an example type lattice, where the arrows represent monotonic (going only
in one direction) loss of information:

<!--
digraph G {
    rankdir="BT";
    empty -> const_int;
    empty -> const_str;
    const_int -> int;
    const_str -> str;
    int -> class_union;
    str -> class_union;
    class_union -> any;

    empty [label="Empty"];
    any [label="Any"];
    int [label="Integer"];
    str [label="String"];
    const_str [label="String[N]"];
    const_int [label="Integer[N]"];
    class_union [label="Class[A, B, C, ...]"];
}
-->

<figure style="display: block; margin: 0 auto;">
  <img src="/assets/img/typelattice.svg" alt="A diagram of multiple labeled sets: Empty,
  known Integer constants, Integer, known String constants, String, a set of
  classes, and the topmost set of all objects, Any. The diagram is vertical, with
  arrows going from bottom to top with decreasing specificity." />
  <figcaption>An example type lattice. At the bottom are the more specific
  types and at the top are the less specific types. An arrow from A to B
  indicates that A is a subtype of B. Arrows also indicate that results can
  only become less precise as more merging happens.</figcaption>
</figure>

This is great. Solves all of our problems, right?

...except, remember we said that we also want to think about specific known
instances if possible. The compiler wants to know about specific values for
optimization, such as `LongExact[5]` or `ObjectUser[C:0xdeadbeef]`, etc. If we
have this information, we can do some partial evaluation, constant-fold
branches, and more.

In the compiler, we also want to have a way to distinguish between the type
*object* `int` and the set of all instances of `int`---type objects are objects
at run-time like everything else.

Let's start off with a bitset. That solves our type lattice problem.

```c++
class Type {
public:
    Type(uint64_t bits) : bits(bits) {}
    Type meet(Type other) { return Type(bits | other.bits); }
    Type intersect(Type other) { return Type(bits & other.bits); }

private:
    uint64_t bits;
};

constexpr Type TTop       = Type(0xfffffffffffUL);
// Some arbitrary bit; we'll come back to choosing the bits later.
constexpr Type TLongExact = Type(0x00010000000UL);
constexpr Type TBottom    = Type(0x00000000000UL);
```

Let's think about the specializations of `Type` that we also want to represent:

* Unspecialized (top!)
* A specific object
* A specific type, exactly that type
* A type or its subclasses
* Unreachable (bottom; only for `TBottom`)

This sounds like another lattice. If we steal some bits from the main type
lattice bitset, we can use those bits to tell us which part of a C `union` is
active:

```c++
class Type {
public:
    // ...

private:
    uint64_t bits_ : kNumTypeBits;  // Some number <= 61 (64 - 3)
    static constexpr int kSpecBits = 3;
    uint64_t spec_kind_ : kSpecBits;
    union {
        PyObject *obj;
        PyTypeObject *type;
    } spec;
};
```

That's cool and all but our `meet` and `intersect` functions no longer make as
much sense---we ignore the specialization, and we shouldn't. We could choose to
always set it to `Top`, but that leaves some optimizations on the table.
Consider the following Python situation:

```python
class C:
    pass

class D(C):
    pass
```

Let's say we have an instance of each of the classes:

* `ObjectUser[C:0xdeadbeef]`, which represents the type bits set for "a user
  subclass of `object`", specialized with a specific heap object at address
  `0xdeadbeef` with class `C`
* `ObjectUser[D:0xfeedcafe]`, which represents the type bits set for "a user
  subclass of `object`", specialized with a specific heap object at address
  `0xfeedcafe` with class `D`

If we try to union the two instances, we'll keep the `ObjectUser` type bits,
but we actually know more information than `Top` about the specialization. We
know that `D` is a subtype of `C`, so we should be able to give the result an
exact type specialization of `C`. This lets us end up with `ObjectUser[C]`,
which could still let us optimize some stuff.

It's a bit tricky to reason about, but that's the main idea. Check out the
[Cinder implementation][type-meet].

[type-meet]: https://github.com/facebookincubator/cinderx/blob/9197ff2a80517304e194ea36b71f973b7daa1bd9/Jit/hir/type.cpp#L491

Now, these type bits are a pain to write down by hand. Instead, it would be
nice to generate them automatically given a class hierarchy. That's what Cinder
does.

## Generating this from a type hierarchy

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

Check out the [Cinder implementation][generate-type].

[generate-type]: https://github.com/facebookincubator/cinderx/blob/9197ff2a80517304e194ea36b71f973b7daa1bd9/Jit/hir/generate_jit_type_h.py

## Some extras

Cinder also has some other features embedded into the same type bitset:

* Primitive type bits and primitive specializations (`CInt32`, `CDouble`,
  `CBool`, ...)
* A lifetime lattice to support [immortal objects][immortal-pep]

[immortal-pep]: https://peps.python.org/pep-0683/

See also the [type.md][type-md] document which goes into detail about Cinder's
type system.

[type-md]: https://github.com/facebookincubator/cinderx/blob/08ec283e5eaf29196d92f8d308aaadc8e82bc0c0/Jit/hir/type.md

## In other compilers

* HHVM has a similar looking [type.h][hhvm-type] and
  [specialization][hhvm-type-spec]. It's similar looking because both Cinder's
  and HHVM's were written by my friend and former coworker Brett Simmers!
* iv, written by Constellation (JavaScriptCore committer), has a [smaller bitset
  lattice][iv-type]
* V8's Turbofan compiler has [something similar][turbofan-type]
* ???

[hhvm-type]: https://github.com/facebook/hhvm/blob/11b663fdfde613d477f38af04db15f7ec1ee9bf3/hphp/runtime/vm/jit/type.h
[hhvm-type-spec]: https://github.com/facebook/hhvm/blob/11b663fdfde613d477f38af04db15f7ec1ee9bf3/hphp/runtime/vm/jit/type-specialization-inl.h
[iv-type]: https://github.com/Constellation/iv/blob/64c3a9c7c517063f29d90d449180ea8f6f4d946f/iv/lv5/breaker/type.h
[turbofan-type]: https://github.com/v8/v8/blob/30be5d03036d6934e847c733315d527915207e85/src/compiler/turbofan-types.h

---
title: "Load and store forwarding in the Toy Optimizer"
layout: post
---

Another entry in the 

A long, long time ago (two years!) CF Bolz-Tereick and I made a [video about
load/store forwarding][video] and accompanying [GitHub Gist][gist] about
load/store forwarding in the Toy Optimizer. I said I would write a blog post
about it, but never found the time---it got lost amid a sea of large life
changes.

[video]: https://www.youtube.com/watch?v=w-UHg0yOPSE
[gist]: https://gist.github.com/tekknolagi/4e3fa26d350f6d3b39ede40d372b97fe

It's a neat idea: do an abstract interpretation over the trace, modeling the
heap at compile-time, removing redundant reads and writes. That means it's
possible to optimize traces like this:

```
v0 = ...
v1 = load(v0, 5)
v2 = store(v0, 6, 123)
v3 = load(v0, 6)
v4 = load(v0, 5)
v5 = do_something(v1, v3, v4)
```

into traces like this:

```
v0 = ...
v1 = load(v0, 5)
v2 = store(v0, 6, 123)
v5 = do_something(v1, 123, v1)
```

This indicates that we were able to remove two redundant loads by keeping
around information about previous loads and stores. Let's get to work making
this possible.

## The usual infrastructure

We'll start off with the usual infrastructure from the [Toy
Optimizer series][toy-optimizer]: a very stringly-typed representation of a
[trace-based SSA IR][toy-ir] and a union-find rewrite mechamism.

[toy-optimizer]: https://pypy.org/categories/toy-optimizer.html
[toy-ir]: https://gist.github.com/tekknolagi/4e3fa26d350f6d3b39ede40d372b97fe#file-port-py-L4-L112

This means we can start writing some new optimization pass and our first test:

```python
def optimize_load_store(bb: Block):
    opt_bb = Block()
    # TODO: copy an optimized version into opt_bb
    return opt_bb

def test_two_loads():
    bb = Block()
    var0 = bb.getarg(0)
    var1 = bb.load(var0, 0)
    var2 = bb.load(var0, 0)
    bb.escape(var1)
    bb.escape(var2)
    opt_bb = optimize_load_store(bb)
    assert bb_to_str(opt_bb) == """\
var0 = getarg(0)
var1 = load(var0, 0)
var2 = escape(var1)
var3 = escape(var1)"""
```

This test is asserting that we can remove duplicate loads. Why load twice if we
can cache the result? Let's make that happen.

## Caching loads

To do this, we'll model the the heap at compile-time. When I say "model", I
mean that we will have an imprecise but correct abstract representation of the
heap: we don't (and can't) have knowledge of every value, but we can know for
sure that some addresses have certain values.

For example, if we have observed a load from object *O* at offset *8* `v0 =
load(O, 8)`, we know that the SSA value `v0` is at `heap[(O, 8)]`. That sounds
tautological, but it's not. Future loads can make use of this information.

```python
def get_num(op: Operation, index: int=1):
    assert isinstance(op.arg(index), Constant)
    return op.arg(index).value

def optimize_load_store(bb: Block):
    opt_bb = Block()
    # Stores things we know about the heap at... compile-time.
    # Key: an object and an offset pair acting as a heap address
    # Value: a previous SSA value we know exists at that address
    compile_time_heap: Dict[Tuple[Value, int], Value] = {}
    for op in bb:
        if op.name == "load":
            obj = op.arg(0)
            offset = get_num(op, 1)
            load_info = (obj, offset)
            previous = compile_time_heap.get(load_info)
            if previous is not None:
                op.make_equal_to(previous)
                continue
            compile_time_heap[load_info] = op
        opt_bb.append(op)
    return opt_bb
```

This pass records information about loads and uses the result of a previous
cached load operation if available. We treat the pair of (SSA value, offset) as
an address into our abstract heap.

That's great! If you run our simple test, it should now pass. But what happens
if we store into that address before the second load? Oops...

```python
def test_store_to_same_object_offset_invalidates_load():
    bb = Block()
    var0 = bb.getarg(0)
    var1 = bb.load(var0, 0)
    var2 = bb.store(var0, 0, 5)
    var3 = bb.load(var0, 0)
    bb.escape(var1)
    bb.escape(var3)
    opt_bb = optimize_load_store(bb)
    assert bb_to_str(opt_bb) == """\
var0 = getarg(0)
var1 = load(var0, 0)
var2 = store(var0, 0, 5)
var3 = load(var0, 0)
var4 = escape(var1)
var5 = escape(var3)"""
```

## Invalidating cached loads

So it turns out we have to also model stores in order to cache loads correctly.
One valid, albeit aggressive, way to do that is to throw away all the
information we know at each store operation:

```python
def optimize_load_store(bb: Block):
    opt_bb = Block()
    compile_time_heap: Dict[Tuple[Value, int], Value] = {}
    for op in bb:
        if op.name == "store":
            compile_time_heap.clear()
        elif op.name == "load":
            # ...
        opt_bb.append(op)
    return opt_bb
```

That makes our test pass---yay!---but at great cost. It means any store
operation mucks up redundant loads. In our world where we frequently read from
and write to objects, this is what we call a huge bummer.

For example, a store to offset 4 on some object should never interfere with a
load from a different offset on the same object[^size]. We should be able to
keep our load from offset 0 cached here:

[^size]: In this toy optimizer example, we are assuming that all reads and writes
    are the same size and different offsets don't overlap at all. This is often
    the case for managed runtimes, where object fields are pointer-sized and
    all reads/writes are pointed aligned.

```python
def test_store_to_same_object_different_offset_does_not_invalidate_load():
    bb = Block()
    var0 = bb.getarg(0)
    var1 = bb.load(var0, 0)
    var2 = bb.store(var0, 4, 5)
    var3 = bb.load(var0, 0)
    bb.escape(var1)
    bb.escape(var3)
    opt_bb = optimize_load_store(bb)
    assert bb_to_str(opt_bb) == """\
var0 = getarg(0)
var1 = load(var0, 0)
var2 = store(var0, 4, 5)
var3 = escape(var1)
var4 = escape(var1)"""
```

We could try instead checking if our specific (object, offset) pair is in the
heap and only removing cached information about that offset and that object.
That would definitely help!

```python
def optimize_load_store(bb: Block):
    opt_bb = Block()
    compile_time_heap: Dict[Tuple[Value, int], Value] = {}
    for op in bb:
        if op.name == "store":
            load_info = (op.arg(0), get_num(op, 1))
            if load_info in compile_time_heap:
                del compile_time_heap[load_info]
        elif op.name == "load":
            # ...
        opt_bb.append(op)
    return opt_bb
```

It makes our test pass, too, which is great news.

Unfortunately, this runs into problems due to aliasing: it's entirely possible
that our heap could contain a pair `(v0, 0)` and a pair `(v1, 0)` where `v0`
and `v1` are the same object (but not known to the optimizer). Then we might
run into a situation where we incorrectly cache loads because the optimizer
doesn't know our abstract addresses `(v0, 0)` and `(v1, 0)` are actually the
same pointer at run-time.

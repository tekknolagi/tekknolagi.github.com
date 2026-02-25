---
title: "A fuzzer for the Toy Optimizer"
layout: post
---

It's hard to get optimizers right. Even if you build up a painstaking test
suite by hand, you will likely miss corner cases, especially corner cases at
the interactions of multiple components or multiple optimization passes.

I wanted to see if I could write a fuzzer to catch some of these bugs
automatically. But a fuzzer alone isn't much use without some correctness
oracle---in this case, we want a more interesting bug than accidentally
crashing the optimizer. We want to see if the optimizer introduces a
correctness bug in the program.

So I set off in the most straightforward way possible, inspired by my
hazy memories of a former [CF blog post][cf-gc-fuzz].

[cf-gc-fuzz]: https://pypy.org/posts/2024/03/fixing-bug-incremental-gc.html

## Generating programs

Generating random programs isn't so bad. We have program generation APIs and we
can dynamically pick which ones we want to call. I wrote a small loop that
generates `load`s from and `store`s to the arguments at random offsets and with
random values, and `escape`s to random instructions with outputs. The idea
with the `escape` is to keep track of the values as if there was some other
function relying on them.

```python
def generate_program():
    bb = Block()
    args = [bb.getarg(i) for i in range(3)]
    num_ops = random.randint(0, 30)
    ops_with_values = args[:]
    for _ in range(num_ops):
        op = random.choice(["load", "store", "escape"])
        arg = random.choice(args)
        a_value = random.choice(ops_with_values)
        offset = random.randint(0, 4)
        if op == "load":
            v = bb.load(arg, offset)
            ops_with_values.append(v)
        elif op == "store":
            value = random.randint(0, 10)
            bb.store(arg, offset, value)
        elif op == "escape":
            bb.escape(a_value)
        else:
            raise NotImplementedError(f"Unknown operation {op}")
    return bb
```

This generates random programs. Here is an example stringified random program:

```
var0 = getarg(0)
var1 = getarg(1)
var2 = getarg(2)
var3 = load(var2, 0)
var4 = load(var0, 1)
var5 = load(var1, 1)
var6 = escape(var0)
var7 = store(var0, 2, 3)
var8 = store(var2, 0, 7)
```

No idea what would generate something like this, but oh well.

## Verifying programs

Then we want to come up with our invariants. I picked the invariant that, under
the same preconditions, the heap will look the same after running an optimized
program as it would under an un-optimized program[^equivalence]. So we can delete
instructions, but if we don't have a load-bearing store, store the wrong
information, or cache stale loads, we will probably catch that.

[^equivalence]: CF notes that this notion of equivalence works for this
    optimizer but not for one that does allocation removal (escape analysis).
    If we removed allocations and writes to them, we would be changing the heap
    results and our verifier would appear to fail. This means we have to, if we
    are to delete allocations, pick a more subtle definition of equivalence.

    Perhaps something that looks like escape analysis in the verifier's
    interpreter?

```python
def verify_program(bb):
    before_no_alias = interpret_program(bb, ["a", "b", "c"])
    a = "a"
    before_alias = interpret_program(bb, [a, a, a])
    optimized = optimize_load_store(bb)
    after_no_alias = interpret_program(optimized, ["a", "b", "c"])
    after_alias = interpret_program(optimized, [a, a, a])
    assert before_no_alias == after_no_alias
    assert before_alias == after_alias
```

I have a very silly verifier that tests two cases: one where the arguments do
not alias and one where they are all the same object. Generating partial
aliases would be a good extension here.

Last, we have the interpreter.

## Running programs

The interpreter is responsible for keeping track of the heap (as indexed by
`(object, offset)` pairs) as well as the results of the various instructions.

We keep track of the `escape`d values so we can see results of some
instructions even if they do not get written back to the heap. Maybe we should
be `escape`ing all instructions with output instead of only random ones. Who
knows.

```python
def interpret_program(bb, args):
    heap = {}
    ssa = {}
    escaped = []
    for op in bb:
        if op.name == "getarg":
            ssa[op] = args[get_num(op, 0)]
        elif op.name == "store":
            obj = ssa[op.arg(0)]
            offset = get_num(op, 1)
            value = get_num(op, 2)
            heap[(obj, offset)] = value
        elif op.name == "load":
            obj = ssa[op.arg(0)]
            offset = get_num(op, 1)
            value = heap.get((obj, offset), "unknown")
            ssa[op] = value
        elif op.name == "escape":
            value = op.arg(0)
            if isinstance(value, Constant):
                escaped.append(value.value)
            else:
                escaped.append(ssa[value])
        else:
            raise NotImplementedError(f"Unknown operation {op.name}")
    heap["escaped"] = escaped
    return heap
```

Then we return the heap so that the verifier can check.

## The harness

Then we run a bunch of random tests through the verifier!

```python
def test_random_programs():
    # Remove random.seed if using in CI... instead print the seed out so you
    # can reproduce crashes if you find them
    random.seed(0)
    num_programs = 100000
    for i in range(num_programs):
        program = generate_program()
        verify_program(program)
```

The number of programs is configurable. Or you could make this `while True`.
But due to how simple the optimizer is, we will find all the possible bugs
pretty quickly.

I initially started writing this post because I thought I had found a bug, but
it turns out that I had, with CF's help, in 2022, walked through every possible
case in the "buggy" situation, and the optimizer handles those cases correctly.
That explains why the verifier didn't find that bug!

## Testing the verifier

So does it work? If you run it, it'll hang for a bit and then report no issues.
That's helpful, in a sense... it's revealing that it is unable to find a
certain class of bug in the optimizer.

Let's comment out the main load-bearing pillar of correctness in the
optimizer---removing aliasing writes---and see what happens.

We get a crash nearly instantly:

```
$ uv run --with pytest pytest loadstore.py -k random
...
=========================================== FAILURES ============================================
_____________________________________ test_random_programs ______________________________________

    def test_random_programs():
        random.seed(0)
        num_programs = 100000
        for i in range(num_programs):
            program = generate_program()
>           verify_program(program)

loadstore.py:617:
_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _

bb = [Operation(getarg, [Constant(0)], None, None), Operation(getarg, [Constant(1)], None, None), Operation(getarg, [Consta...], None, None)], None, None), Operation(load, [Operation(getarg, [Constant(0)], None, None), Constant(0)], None, None)]

    def verify_program(bb):
        before_no_alias = interpret_program(bb, ["a", "b", "c"])
        a = "a"
        before_alias = interpret_program(bb, [a, a, a])
        optimized = optimize_load_store(bb)
        after_no_alias = interpret_program(optimized, ["a", "b", "c"])
        after_alias = interpret_program(optimized, [a, a, a])
        assert before_no_alias == after_no_alias
>       assert before_alias == after_alias
E       AssertionError: assert {('a', 0): 4,...', 3): 1, ...} == {('a', 0): 9,...', 3): 1, ...}
E
E         Omitting 4 identical items, use -vv to show
E         Differing items:
E         {('a', 0): 4} != {('a', 0): 9}
E         Use -v to get more diff

loadstore.py:610: AssertionError
==================================== short test summary info ====================================
FAILED loadstore.py::test_random_programs - AssertionError: assert {('a', 0): 4,...', 3): 1, ...} == {('a', 0): 9,...', 3): 1, ...}
=============================== 1 failed, 15 deselected in 0.04s ================================
$
```

We should probably use `bb_to_str(bb)` and `bb_to_str(optimized)` to print out
the un-optimized and optimized traces in the `assert` failure messages. But we
get a nice diff of the heap automatically, which is neat. And it points to an
aliasing problem!

## Full code

See the [full code](https://github.com/tekknolagi/tekknolagi.github.com/blob/fbccf9696e98721ca77c8d5ec5f828a11492b04c/loadstore.py).

## Extensions

* Synthesize (different) types for non-aliasing objects and add them in `info`
* Shrink/reduce failing examples down for easier debugging
* Use Hypothesis for property-based testing, which CF notes also gives you
  shrinking
* [Use Z3 to encode][cf-smt-fuzz] the generated programs instead of randomly interpreting them

[cf-smt-fuzz]: https://pypy.org/posts/2022/12/jit-bug-finding-smt-fuzzing.html

## Thanks

Thank you to [CF Bolz-Tereick](https://cfbolz.de/) for feedback on this post!

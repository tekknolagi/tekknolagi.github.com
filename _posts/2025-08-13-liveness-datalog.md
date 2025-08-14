---
title: "Liveness analysis with Datalog"
layout: post
co_authors: Waleed Khan
---

After publishing [Linear scan register allocation on SSA](/blog/linear-scan), I
had a nice call with [Waleet Khan](https://waleedkhan.name) where he showed me
how to Datalog. He thought it might be useful to try implementing liveness
analysis as a Datalog problem.

We started off with the Wimmer2010 CFG example from that post sketching out
manually which variables were live out of each block: R10 out of B1, R12 out of
B2, etc.

<figure>
<object class="svg" type="image/svg+xml" data="/assets/img/wimmer-lsra-cfg.svg"></object>
<figcaption markdown=1>
The graph from Wimmer2010 has come back! Remember, we're using block arguments
instead of phis, so `B1(R10, R11)` defines R10 and R11 before the first
instruction in B1.
</figcaption>
</figure>

Then we tried to formulate liveness as a Datalog relation.

Liveness is normally (at least for me) defined in terms of two relations:
live-in and live-out. Live-out is "what is needed" from all of the successors
of a block and live-in is the "what is needed" summary for a block. So:

```
live-out(b) = union(live-in(s) for each successor s of b)
live-in(b) = (live-out(b) + used(b)) - defined(b)
```

where each of the component parts of that expression represent sets of
variables:

* *used(b)* is the set of variables referenced as in-operands to instructions in
  a block
* *defined(b)* is the set of variables defined by instructions in a block

We ended up computing the live-in sets for blocks in the register allocator
post but then using the live-out sets instead. So today let's compute some
live-out sets with Datalog!

## Datalog

We'll be using Souffle here because Waleed mentioned it and also I learned a
bit about it in my databases class.

The thing you do first is define your relations. In this case, if we want to
compute liveness information, we have to know information about what a block
uses, defines, and what successors it has.

First, the thing you have to know about Datalog, is that it's kind of like
the opposite of array programming. We're going to express things about sets by
expressing facts about individual items in a set.

For example, we're not going to say "this block B4 uses [R10, R12, R16]". We're
going to say three separate facts: "B4 uses R10", "B4 uses R12", "B4 uses R16".
You can think about it like each relation being a database table where each
parameter is a column name.

Here are the relations for block uses, block defs, and which blocks follow
other blocks:

```
// liveness.dl
.decl block_use(block:symbol, var:symbol)
.decl block_def(block:symbol, var:symbol)
.decl block_succ(succ:symbol, pred:symbol)
```

Where `symbol` here means string.

We can then embed some facts inline. For example, this says "A defines R0 and
R1 and uses R0":

```
block_def("A", "R0").
block_def("A", "R1").
block_use("A", "R0").
```

You can also provide facts as a TSV but this file format is so irritating to
construct manually and has given me silently wrong answers in Souffle before so
I am not doing that for this example.

You can, for your edification, manually encode all the use/def/successor facts
from the previous post into Souffle---or you can copy this chunk into your file:

```
// liveness.dl
// ...
block_def("B1", "R10").
block_def("B1", "R11").
block_use("B1", "R11").

block_def("B2", "R12").
block_def("B2", "R13").
block_use("B2", "R13").

block_def("B3", "R14").
block_def("B3", "R15").
block_use("B3", "R12").
block_use("B3", "R13").
block_use("B3", "R14").
block_use("B3", "R15").

block_def("B4", "R16").
block_use("B4", "R16").
block_use("B4", "R10").
block_use("B4", "R12").

block_succ("B2", "B1").
block_succ("B3", "B2").
block_succ("B2", "B3").
block_succ("B4", "B2").
```

We can declare our live-in and live-out relations similarly to our use/def/succ
relations. We mark them as being `.output` so that Souffle presents us with the
results.

```
// liveness.dl
// ...
.decl live_out(block:symbol, var:symbol)
.output live_out
.decl live_in(block:symbol, var:symbol)
.output live_in
```

Now it's time to define our relations. You may notice that the Souffle
definitions look very similar to our earlier definitions. This is no mistake;
Datalog was created for dataflow and graph problems.

We'll start with live-out:

```
// liveness.dl
// ...
live_out(b, v) :- block_succ(s, b), live_in(s, v).
```

We read this left to right as "a variable `v` is live-out of block `b` if block
`s` is a successor of `b` and `v` is live-in to `s`". The `:-` defines the left
side in terms of the right side. The comma between `block_succ` and `live_in`
means it's a conjunction---*and*.

Where's the union? Well, remember what I said about array programming? We're
not thinking in terms of sets. We're thinking one program variable at a time.
As Souffle executes our relations, `live_out` will incrementally build up a
table.

It's also a little weird to program in this style because `s` wasn't textually
defined anywhere like a parameter or a variable. You kind of have to think of
`s` as connector, a binder, a foreign key---what have you. It's a placeholder.
(I don't know how to explain this well. Sorry.)

Then we can define live-in. This on the surface looks more complicated but I
think that is only because of Souffle's choice of syntax.

```
// liveness.dl
// ...
live_in(b, v) :- (live_out(b, v); block_use(b, v)), !block_def(b, v).
```

It reads as "a variable `v` is live-in to `b` if it is either live-out of `b`
or used in `b`, and *not* defined in `b`. The semicolons are
disjunctions---*or*---and the exclamation points negations---*not*.

These functions look endlessly mutually recursive but you have to keep in mind
that we're not running functions on data, exactly. We're declaratively
expressing definitions of rules---relations. `block_use(b, v)` in the body of
`live_in` is not calling a function but instead making a query---is the row
`(b, v)` in the table `block_use`? Datalog builds the tables until saturation.

Now we can run Souffle! We tell it to dump to standard output with `-D-` but
you could just as easily have it dump each output relation in its own separate
file in the current directory by specifying `-D.`.

```console
$ souffle -D- liveness.dl
---------------
live_in
block   var
===============
B2      R10
B3      R10
B3      R12
B3      R13
B4      R10
B4      R12
===============
---------------
live_out
block   var
===============
B1      R10
B2      R10
B2      R12
B2      R13
B3      R10
===============
$
```

That's neat. We got nicely formatted tables and it only took us two lines of
code! This is because we have separated the iteration-to-fixpoint bit from the
main bit of the dataflow analysis: the equation. If we let Datalog do the data
movement for us, we can work on the rules---and only the rules.

> This is probably why, in the fullness of time, many static analysis and
> compiler tools end up growing some kind of embedded (partial) Datalog engine.
> Call it Scholz's tenth rule.

Souffle also has the ability to compile to C++, which gives you two nice
things:

1. you can probably get faster execution
1. you can use it from an existing C++ program

I don't have any experience with this API.

This is when Waleed mentioned offhandedly that he had heard about some embedded
Rust datalog called [Ascent](https://s-arash.github.io/ascent/).

## Rust

The front page of the Ascent website is a really great sell if you show up
thinking "gee, I wish I had Datalog to use in my Rust program". Right out the
gate, you get reasonable-enough Datalog syntax via a proc macro.

For example, here is the canonical path example for Souffle:

```
.decl edge(x:number, y:number)
.decl path(x:number, y:number)

path(x, y) :- edge(x, y).
path(x, y) :- path(x, z), edge(z, y).
```

and in Ascent:

```rust
ascent! {
   relation edge(i32, i32);
   relation path(i32, i32);

   path(x, y) <-- edge(x, y);
   path(x, z) <-- edge(x, y), path(y, z);
}
```

Super.

We weren't sure if the Souffle liveness would port cleanly to Rust, but it sure
did! It even lets you use your own datatypes instead of just `i32` (which the
front-page example uses).

```rust
use ascent::ascent;

#[derive(Clone, PartialEq, Eq, Hash, Copy)]
struct BlockId(i32);

impl std::fmt::Debug for BlockId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "B{}", self.0)
    }
}

#[derive(Clone, PartialEq, Eq, Hash, Copy)]
struct VarId(i32);

impl std::fmt::Debug for VarId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "R{}", self.0)
    }
}

ascent! {
    relation block_use(BlockId, VarId);
    relation block_def(BlockId, VarId);
    relation block_succ(BlockId, BlockId);  // (succ, pred)
    relation live_out(BlockId, VarId);
    relation live_in(BlockId, VarId);
    live_out(b, v) <-- block_succ(s, b), live_in(s, v);
    live_in(b, v) <-- (live_out(b, v) | block_use(b, v)), !block_def(b, v);
}
fn main() {
    // ...
}
```

Notice how we don't have an `input` or `output` annotation like we did in
Datalog. That's because this is designed to be embedded in an existing program,
which probably doesn't to deal with the disk (or at least wants to read/write
in its own format).

Ascent lets us give it some vectors of data and then at the end lets us read
some vectors of data too.

```rust
// ...
fn main() {
    let mut prog = AscentProgram::default();
    let b1 = BlockId(1);
    let b2 = BlockId(2);
    let b3 = BlockId(3);
    let b4 = BlockId(4);
    let r10 = VarId(10);
    let r11 = VarId(11);
    let r12 = VarId(12);
    let r13 = VarId(13);
    let r14 = VarId(14);
    let r15 = VarId(15);
    let r16 = VarId(16);
    prog.block_def = vec![
        (b1, r10),
        (b1, r11),
        (b2, r12),
        (b2, r13),
        (b3, r14),
        (b3, r15),
        (b4, r16),
    ];
    prog.block_succ = vec![
        (b2, b1),
        (b3, b2),
        (b2, b3),
        (b4, b2),
    ];
    prog.block_use = vec![
        (b1, r11),
        (b2, r13),
        (b3, r12),
        (b3, r13),
        (b3, r14),
        (b3, r15),
        (b4, r10),
        (b4, r12),
        (b4, r16),
    ];
    prog.run();
    println!("live out: {:?}", prog.live_out);
    println!("live out: {:?}", prog.live_in);
}
```

Then we need only run `cargo add ascent` and `cargo run`---both of which worked
with zero issues---and see the results.

```console
$ cargo run
    Finished `dev` profile [unoptimized + debuginfo] target(s) in 0.02s
     Running `target/debug/liveness`
live out: [(B2, R12), (B2, R13), (B2, R10), (B1, R10), (B3, R10)]
live out: [(B3, R12), (B3, R13), (B4, R10), (B4, R12), (B2, R10), (B3, R10)]
$
```

It's not a fancy looking table, but it's very close to my program, which is
neat.

> This is similar to embedding Souffle in C++ and then calling the C++. One
> difference, though, is the Souffle process has two steps. It's a slight build
> system complication. But this isn't meant to be a Datalog comparison post!

TODO

## More?

Can we model all of linear scan this way? Maybe. I'm new to all this stuff.

Lattices

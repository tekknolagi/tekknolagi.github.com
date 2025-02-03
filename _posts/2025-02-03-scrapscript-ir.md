---
title: A compiler IR for Scrapscript
layout: post
---

I wrote previously about different slices of life in the various
implementations of the Scrapscript programming language. Until two weeks ago,
the most interesting implementation was a so-called ["baseline
compiler"](/blog/scrapscript-baseline/) directly from the AST to C. Now there
we have an intermediate representation---an IR.

## Why add an IR?

The AST is fine. It's a very abstract representation of the progam and it is
easy to generate directly from the source text. None of the names are
resolved (there are a bunch of strings everywhere), there is a lot left
implicit in the representation, and evaluating it requires having a stack or
recursion to keep temporary data around. But hey, it works and it's reasonably
small.

It's possible to incrementally rewrite the AST by adding new types of nodes or
folding existing ones together. For example, it's possible to find patterns
similar to `Binop(BinopKind.ADD, Int(2), Int(3))` and rewrite them directly
into `Int(5)`. This kind of optimization either doesn't go very far or also
requires keeping significant amounts of context on the side.

It's also possible to try and retrofit some of this constant folding into other
AST passes that run anyway. You can kind of see this in the post about the
[compiler tricks](/blog/scrapscript-tricks/) where we opportunistically fold
constant data into its own section so it does not need to be heap allocated.

Instead, I chose to create a new program representation designed to be
optimized and rewritten: an SSA IR. True to the spirit of the project, it's a
home-grown one, not some existing library like LLVM.

This is partly because it keeps things simple---the IR, some optimization
passes, and the compiler from the IR to C fit neatly into ~1000 lines of
code---and partly because projects like LLVM are not designed for languages
like this. They can probably do some impressive things, but they would do
better if purpose-built domain-specific analyses unwrapped some of the
functional abstractions first.

Still, you might be wondering why we then compile to our IR C instead of LLVM.
Two reasons: 1) we already have an existing C runtime and 2) I really, really,
really do not want to be tied to a particular LLVM version and its quirks. One
of the most frustrating things when hacking on someone else's language project
is watching it download a huge LLVM bundle and then compile it. It does a heck
of a lot. It's very impressive. It's many person-lifetimes of engineering
marvels. But it's too big for this dinky little tugboat of a project.

## What does the IR look like?

## What's up with SSA?

## Some optimization passes

## Testing

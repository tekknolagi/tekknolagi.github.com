---
title: "Precedence climbing"
layout: post
---

I wrote a sample precedence-climbing expression parser. [Patrick
Dubroy](https://dubroy.com/blog/) hosts an excellent [small website for small
programs](https://pdubroy.github.io/200andchange/), I put my implementation
there. It uses docco to pull out code comments into a side pane next to the
code and make everything look pretty.

Take a look at [my
implementation](https://pdubroy.github.io/200andchange/precedence-climbing/),
which is a tokenizer and parser for simple math expressions and function calls
in under 200 SLOC. It builds an "AST" in the form of nested Python lists that
kind of look like S-expressions (but you could also make classes for AST nodes
if you like). It also comes with tests. It works standalone or can be used
as the "simple expression parser" component of a larger recursive descent
parser (that handles function definitions, variable binding, etc).

See also [Parsing to IR and lvalues](/blog/ir-lvalues/) which takes this
approach but builds an SSA based IR instead of an AST.

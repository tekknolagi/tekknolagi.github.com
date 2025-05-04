---
title: "Precedence climbing"
layout: post
---

I wrote a [sample precedence-climbing expression
parser](https://pdubroy.github.io/200andchange/precedence-climbing/). I wrote
it for [Patrick Dubroy](https://dubroy.com/blog/)'s excellent [small website
for small programs](https://pdubroy.github.io/200andchange/), which uses docco
to pull out code comments into a side pane next to the code and make everything
look pretty.

My implementation is a tokenizer and parser for simple math expressions and
function calls in under 200 SLOC. It builds an "AST" in the form of nested
Python lists that kind of look like S-expressions (but you could also make
classes for AST nodes if you like).

```console?lang=python&prompt=>>>,...
>>> tokenize("1+2*3")
[1, '+', 2, '*', 3]
>>> parse(_)
['+', 1, ['*', 2, 3]]
>>>
```

It also comes with tests. It works standalone or can be used as the "simple
expression parser" component of a larger recursive descent parser (that handles
function definitions, variable binding, etc).

See also [Parsing to IR and lvalues](/blog/ir-lvalues/) which takes this
approach but builds an SSA based IR instead of an AST.

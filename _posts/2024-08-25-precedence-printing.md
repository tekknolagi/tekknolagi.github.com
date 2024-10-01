---
title: Precedence-aware pretty printing
layout: post
date: 2024-08-25
---

*This post follows from a conversation with [Chris
Gregory](https://www.chrisgregory.me/) about parsing and pretty printing. We
might integrate some or all of it into
[Scrapscript](https://github.com/tekknolagi/scrapscript)!*

I have some math expressions that I want to print infix, like
`Add(Const(1), Mul(Const(2), Const(3)))`.

```python
@dataclass
class Expr:
    pass

@dataclass
class Const(Expr):
    value: int | float

@dataclass
class Binary(Expr):
    left: Expr
    right: Expr

@dataclass
class Add(Binary):
    pass

# ...
```

Right now, that example expression prints as `1+(2*3)` because that's the
easiest way to correctly print the expression.

```python
OPS = {
    Add: "+",
    Sub: "-",
    Mul: "*",
    Div: "/",
    Pow: "^",
}


def pretty(expr: Expr) -> str:
    match expr:
        case Const(val):
            return str(val)
        case Binary(left, right):
            op, = OPS[type(expr)]
            return f"({pretty(left)}{op}{pretty(right)})"
    raise NotImplementedError(type(expr))
```

It's not great, but it works. The assumption is that `pretty` will always
return something correctly parenthesized, so calls to `pretty(left)` and
`pretty(right)` in binary operators will result in well-formed expressions and
only the result needs to be parenthesized[^over-paren].

[^over-paren]: This is much better than the alternative, where the handler for
    binary operators instead tries to parenthesize left and right on its own.
    That leads to expressions like `(3)+(4)`.

Ideally, though, we would like that to print as `1+2*3` because the parentheses
are unnecessary. We know that `*` has higher precedence---binds tighter---than
`+`.

I looked for some articles on precedence-aware printing and really only found:

* [this StackOverflow answer][first-stackoverflow] ("Pretty Printing AST with Minimal Parentheses")
* [this StackOverflow answer][second-stackoverflow] ("Minimize parenthesis when printing expression")

[first-stackoverflow]: https://stackoverflow.com/questions/13708837/pretty-printing-ast-with-minimal-parentheses/16685965#16685965
[second-stackoverflow]: https://stackoverflow.com/questions/61159436/minimize-parenthesis-when-printing-expression/61160682#61160682

I also know vaguely of more complicated articles solving problems like line wrapping, such as

* Wadler's [A prettier printer](https://homepages.inf.ed.ac.uk/wadler/papers/prettier/prettier.pdf) (PDF)
* [PPrint](http://cambium.inria.fr/~fpottier/pprint/doc/pprint/)
* [A Prettier JavaScript Formatter](https://archive.jlongster.com/A-Prettier-Formatter)

None of the less formal resources are completely satisfying and the more formal
resources seem long and do more, so I am writing this post to document my
implementation.

## My implementation

First of all, here it is in its entirety.

```python
PREC = {
    Add: ("+", "any", 1),
    Sub: ("-", "left", 1),
    Mul: ("*", "any", 2),
    Div: ("/", "left", 2),
    Pow: ("^", "right", 3),
}


def pretty(expr: Expr, prec: int = 0) -> str:
    match expr:
        case Const(val):
            return str(val)
        case Binary(left, right):
            op, assoc, op_prec = PREC[type(expr)]
            left_prec = op_prec if assoc == "right" else op_prec - 1
            right_prec = op_prec if assoc == "left" else op_prec - 1
            result = f"{pretty(left, left_prec)}{op}{pretty(right, right_prec)}"
            if prec >= op_prec:
                return "(" + result + ")"
            return result
    raise NotImplementedError(type(expr))
```

The first thing to note is that while I don't have a parser in this small
example, if I did, I could reuse its precedence and associativity table for the
printer. The table lays out `+` as the lowest precedence/least tightly binding
operator and `^` as the highest precedence/most tightly binding operator.

If you ignore associativity for a second---just look at expressions that only
contain `+` and `*`---you can see that every recursive call to `pretty` for
subexpressions passes the current operator's precedence (minus one) down. Then,
for the current operator, if we have lower precedence than the parent operator,
we have to parenthesize.

Here's what that looks like:

```python
# Ignore all associativity for a second
def pretty(expr: Expr, prec: int = 0) -> str:
    match expr:
        # ...
        case Add(left, right) | Mul(left, right):
            op, _, op_prec = PREC[type(expr)]
            result = f"{pretty(left, op_prec-1)}{op}{pretty(right, op_prec-1)}"
            if prec >= op_prec:
                return "(" + result + ")"
            return result
```

This means all additions will be "flattened" like `1+2+3` and all
multiplications will be as well. But it will still appropriately parenthesize
mixtures of the two, like differentiating `1+2*3` and `(1+2)*3`.

This falls apart when order matters, like with subtraction and division. Those
associate to the left:

```console?prompt=>>>
>>> 1-2-3
-4
>>> (1-2)-3
-4
>>> 1-(2-3)
2
>>> 1/2/3
0.16666666666666666
>>> (1/2)/3
0.16666666666666666
>>> 1/(2/3)
1.5
>>>
```

To handle that, we need to pass the precedence down differently for left and
right children. If the operators is left associative, we keep the required
precedence on the right the same---don't decrease it.

```python
# Ignore right-associativity for a second
def pretty(expr: Expr, prec: int = 0) -> str:
    match expr:
        # ...
        case Sub(left, right) | Div(left, right):
            op, _, op_prec = PREC[type(expr)]
            result = f"{pretty(left, op_prec-1)}{op}{pretty(right, op_prec)}"
            if prec >= op_prec:
                return "(" + result + ")"
            return result
```

This means all left-nesting subtractions will be "flattened" like
`Sub(Sub(Const(1), Const(2)), Const(3))` will become `1-2-3` (and same with
division), but it will add parentheses with right-nesting and also mixed
precedence.

To handle right-associativity for, say, exponentiation, we do the opposite:

```python
def pretty(expr: Expr, prec: int = 0) -> str:
    match expr:
        # ...
        case Pow(left, right):
            op, _, op_prec = PREC[type(expr)]
            result = f"{rec(left, op_prec)}{op}{rec(right, op_prec-1)}"
            if prec >= op_prec:
                return "(" + result + ")"
            return result
```

Then, if you combine all of this information about precedence and associativity
into a table and use that for lookup, you get my implementation, repeated here:

```python
PREC = {
    Add: ("+", "any", 1),
    Sub: ("-", "left", 1),
    Mul: ("*", "any", 2),
    Div: ("/", "left", 2),
    Pow: ("^", "right", 3),
}


def pretty(expr: Expr, prec: int = 0) -> str:
    match expr:
        case Const(val):
            return str(val)
        case Binary(left, right):
            op, assoc, op_prec = PREC[type(expr)]
            left_prec = op_prec if assoc == "right" else op_prec - 1
            right_prec = op_prec if assoc == "left" else op_prec - 1
            result = f"{pretty(left, left_prec)}{op}{pretty(right, right_prec)}"
            if prec >= op_prec:
                return "(" + result + ")"
            return result
    raise NotImplementedError(type(expr))
```

I *think* this "any" associativity removes the need to "cheat" and peek into
the children as mentioned by the [second StackOverflow
answer][second-stackoverflow].

Let me know if you have suggestions for improvement or if I am completely
missing something. Parsing and precedence and associativity and things like
that for whatever reason seem to bite me more than anything else in compilers.

## See also

River wrote a [follow-up
post](https://k-monk.org/blog/minimal-parenthesization-of-lambda-terms/) for
his Simply-Typed Lambda Calculus compiler in OCaml.

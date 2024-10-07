---
title: Damas-Hindley-Milner inference two ways
layout: post
co_authors: River Dillon Keefer
---

## What is Damas-Hindley-Milner?

Damas-Hindley-Milner (HM) is a type system for the lambda calculus (later
adapted for Standard ML and the ML-family languages) with parametric
polymorphism, aka generic functions. It sits at a sweet spot in PL design: the
type system is quite expressive, and there are well known type inference
algorithms that require absolutely no annotations from the programmer.

It seems to have been discovered independently multiple times over the years,
but the most famous papers are the [original][original-milner] (PDF) by Milner
and the [follow-on][damas-milner] (PDF) by Damas and Milner. Damas continued on
to write his [thesis][damas-thesis] (PDF) about it. (If you have a link to the
appropriate Hindley paper, please let me know.)

[original-milner]: /assets/img/milner-theory-type-polymorphism.pdf

[damas-milner]: /assets/img/damas-milner-original.pdf

[damas-thesis]: /assets/img/damas-thesis.pdf

The type system is limited, but by virtue of being limited, it confers these
advantages:

* Inference algorithms tend to be fast (roughly O(size of code), but there are
  pathological cases if you have tuple or record types)
* Something something principal types
* ??? [It's a good type system, reader!](https://stackoverflow.com/a/399392)

<!--As a general note: the more constrained your language/system is, the more you
can optimize-->

In this post, we implement HM in two ways (W, J, and mention a third, M), and
then extend it a little bit. We'll do this in the context of
[scrapscript](/blog/scrapscript), but the goal is to get a better understanding
of HM in general.

After introducing the data structures, we'll start with Algorithm W because
it's the OG.

## The data structures

Every expression has exactly one type, called a *monotype*. For our purposes, a
monotype is either a type variable like `'a` or the application of a type
constructor like `->` (a function, as in OCaml or Haskell), `list`, etc to
monotype arguments (`'a list`, `'a -> 'b`).

We represent those two kinds in Python with classes:

```python
@dataclasses.dataclass
class MonoType:
    pass


@dataclasses.dataclass
class TyVar(MonoType):
    name: str


@dataclasses.dataclass
class TyCon(MonoType):
    name: str
    args: list[MonoType]
```

A lot of people make HM type inference implementations by hard-coding functions
and other type constructors like `list` as the only type constructors but we
instead model them all in terms of `TyCon`:

```python
IntType = TyCon("int", [])
BoolType = TyCon("bool", [])

def list_type(ty: MonoType) -> MonoType:
    return TyCon("list", [ty])

def func_type(arg: MonoType, ret: MonoType) -> MonoType:
    return TyCon("->", [arg, ret])
```

We'll also have something called a forall (also known as a type scheme,
universal quantification, polytype, etc used for polymorphism), which we'll
talk about more later, but for now is a thin wrapper around a monotype:

```python
@dataclasses.dataclass
class Forall:
    tyvars: list[TyVar]
    ty: MonoType
```

With these, we model the world.

## Algorithm W

Algorithm W is probably the most famous one (citation needed) because it was
presented in the paper as the easiest to prove correct. It's also free of side
effects, which probably appeals to [Haskell nerds][haskell-uf].

[haskell-uf]: https://hackage.haskell.org/package/union-find-array-0.1.0.4/docs/Control-Monad-Union.html

(Because it is side effect free, it requires threading all the state through by
hand. This can look intimidating compared to Algorithm J, where we mutate
global state as we go. If you get discouraged, you might want to skip ahead to
Algorithm J.)

The idea is that you have a function `infer_w` that takes an expression and an
*environment* (a "context") and returns a *substitution* and a type. The type
is the type of the expression that you passed in. We'll use the substitution to
keep track of constraints on types that we learn as we walk the tree. It's a
mapping from type variables to monotypes. As we learn more information, the
substitution will grow.

In Python syntax, that's:

```python
Subst = typing.Mapping[str, MonoType]  # type variable -> monotype
Context = typing.Mapping[str, Forall]  # program variable -> type scheme
def infer_w(expr: Object, ctx: Context) -> tuple[Subst, MonoType]: ...
```

Before diving into the code, let's go over the algorithm in prose. The rules of
inference are as follows:

* if you see an integer literal
  * return `IntType`
* if you see a variable `e`,
  * look up the scheme of `e` in the environment
  * **unwrap the shallow type scheme to get the monotype** (we'll return to
    this later)
* if you see a function `e`,
  * invent a new type variable `t` for the parameter, and add it to the
    environment while type checking the body `b` (call that `type(b)`)
  * return a function type from `t` to `type(b)`
* if you see function application `e`,
  * infer the type of callee `f`
  * infer the type of the argument `a`
  * invent a new type variable `r`
  * constrain `type(f)` to be a function from `type(a)` to `r`
  * return `r`
* if you see a let binding `let n = v in b` (called "where" in scrapscript) `e`,
  * infer the type of the value `v`
  * construct a **superficial type scheme `s` containing `type(v)`** (we'll
    return to this later)
  * add `n: s` to the environment while type checking the body `b`
  * return `type(b)`

In general, we either constrain existing type variables or invent new ones to
stand for types about which we don't yet have complete information.

In order to keep the constraints (substitutions) flowing after each recursive
call to `infer_w`, we need to be able to compose substitutions. It's not just a
union of two dictionaries, but instead more like function composition.

```python
def compose(newer: Subst, older: Subst) -> Subst: ...
```

Let's look at a manual "type inference" session where we incrementally learn
that `a` is found equivalent to `b` (subst 1), then `b` to `c` (subst 2), and
finally that `c` is an `int` (subst 3). These three separate facts must be
combined in order to fully realize that all three type variables are `int`.

```console?lang=python&prompt=>>>
>>> s1 = {"a": TyVar("b")}
>>> s2 = {"b": TyVar("c")}
>>> s3 = {"c": TyCon("int", [])}
>>> compose(s2, s1)
{'a': TyVar(name='c'), 'b': TyVar(name='c')}
>>> compose(s3, compose(s2, s1))
{'a': TyCon(name='int', args=[]),
 'b': TyCon(name='int', args=[]),
 'c': TyCon(name='int', args=[])}
>>>
```

TODO

, `apply_ty`, and `apply_ctx` are for.

```python
def apply_ty(ty: MonoType, subst: Subst) -> MonoType: ...

def apply_ctx(ctx: Context, subst: Subst) -> Context: ...
```

This "constrain" process we talked about in the inference rules refers to
*unification*, which we call `unify_w`. In Algorithm W, unification involves
building up a substitution. Type variables are "easy"; bind them to a monotype.
For type constructors, we have to check that the constructor name matches, then
that they each have the same number of arguments, and finally build up
constraints by unifying the arguments pairwise.

<!-- TODO: occurs check -->

<!--
-- Z combinator doesn't type check, similar to OCaml (we don't have -rectypes)
Z factr 5

. Z = f -> (x -> f (v -> (x x) v)) (x -> f (v -> (x x) v))

. factr = facti ->
  | 0 -> 1
  | n -> n * (facti (n - 1))
-->

```python
def unify_w(ty1: MonoType, ty2: MonoType) -> Subst:
    if isinstance(ty1, TyVar):
        return bind_var(ty2, ty1.name)
    if isinstance(ty2, TyVar):  # Mirror
        return unify_w(ty2, ty1)
    if isinstance(ty1, TyCon) and isinstance(ty2, TyCon):
        if ty1.name != ty2.name:
            unify_fail(ty1, ty2)
        if len(ty1.args) != len(ty2.args):
            unify_fail(ty1, ty2)
        result: Subst = {}
        for l, r in zip(ty1.args, ty2.args):
            result = compose(
                unify_w(apply_ty(l, result), apply_ty(r, result)),
                result,
            )
        return result
    raise TypeError(f"Unexpected type {type(ty1)}")
```

As an example of this pairwise unification, we can see that unifying a `'a
list` with an `int list` means that `'a` gets marked equivalent to `int` in the
substitution:

```console?lang=python&prompt=>>>
>>> ty1 = TyCon("list", [TyVar("a")])
>>> ty2 = TyCon("list", [TyCon("int", [])])
>>> unify_w(ty1, ty2)
{'a': TyCon(name='int', args=[])}
>>>
```

OK, ...

```python
def infer_w(expr: Object, ctx: Context) -> tuple[Subst, MonoType]:
    if isinstance(expr, Var):
        scheme = ctx.get(expr.name)
        if scheme is None:
            raise TypeError(f"Unbound variable {expr.name}")
        return {}, scheme.ty
    if isinstance(expr, Int):
        return {}, IntType
    if isinstance(expr, Function):
        arg_tyvar = fresh_tyvar()
        body_ctx = {**ctx, expr.arg.name: Forall([], arg_tyvar)}
        body_subst, body_ty = infer_w(expr.body, body_ctx)
        return body_subst, TyCon("->", [apply_ty(arg_tyvar, body_subst), body_ty])
    if isinstance(expr, Apply):
        s1, ty = infer_w(expr.func, ctx)
        s2, p = infer_w(expr.arg, apply_ctx(ctx, s1))
        r = fresh_tyvar()
        s3 = unify_w(apply_ty(ty, s2), TyCon("->", [p, r]))
        return compose(s3, compose(s2, s1)), apply_ty(r, s3)
    if isinstance(expr, Where):
        name, value, body = expr.binding.name.name, expr.binding.value, expr.body
        s1, ty1 = infer_w(value, ctx)
        ctx1 = dict(ctx)  # copy
        ctx1.pop(name, None)
        ctx2 = {**ctx, name: Forall([], ty1)}
        s2, ty2 = infer_w(body, apply_ctx(ctx2, s1))
        return compose(s2, s1), ty2
    raise TypeError(f"Unexpected type {type(expr)}")
```

## Algorithm J

Alright, so substitutions are a little clunky. Maybe there's a neat way to do
this in functional languages by threading the state through automatically or
something, but we're in Python and I'm a bit of a programming caveman, so we're
doing side effects.

Unlike Algorithm W, which builds up a map of substitutions, Algorithm J uses
union-find on the type variables to store equivalences. (I wrote about
union-find previously in my intro to [Vectorizing ML
models](/blog/vectorizing-ml-models/).)

We have to add the usual `forwarded`/`find`/`make_equal_to` infrastructure to
the types we defined above.

```python
@dataclasses.dataclass
class MonoType:
    forwarded: MonoType | None = dataclasses.field(init=False, default=None)

    def find(self) -> MonoType:
        # Exercise for the reader: path compression
        result: MonoType = self
        while isinstance(result, TyVar):
            it = result.forwarded
            if it is None:
                return result
            result = it
        return result

    def _set_forwarded(self, other: MonoType) -> None:
        raise NotImplementedError


@dataclasses.dataclass
class TyVar(MonoType):
    name: str

    def make_equal_to(self, other: MonoType) -> None:
        self.find()._set_forwarded(other)

    def _set_forwarded(self, other: MonoType) -> None:
        self.forwarded = other


@dataclasses.dataclass
class TyCon(MonoType):
    name: str
    args: list[MonoType]
```

While it doesn't really make sense to `find` or `make_equal_to` on a type
constructor (it should always be a leaf in the union-find DAG), we still define
`find` and `_set_forwarded` to make MyPy happy and make some code look a little
more natural.

Once we do that, we can write our unify implementation for Algorithm J. You can
see that the general structure has not changed much, but the recursive bits
in the `TyCon` case have gotten much simpler to read.

<!-- TODO: occurs check -->

```python
def unify_j(ty1: MonoType, ty2: MonoType) -> None:
    ty1 = ty1.find()
    ty2 = ty2.find()
    if isinstance(ty1, TyVar):
        ty1.make_equal_to(ty2)
        return
    if isinstance(ty2, TyVar):  # Mirror
        return unify_j(ty2, ty1)
    if isinstance(ty1, TyCon) and isinstance(ty2, TyCon):
        if ty1.name != ty2.name:
            unify_fail(ty1, ty2)
        if len(ty1.args) != len(ty2.args):
            unify_fail(ty1, ty2)
        for l, r in zip(ty1.args, ty2.args):
            unify_j(l, r)
        return
    raise TypeError(f"Unexpected type {type(ty1)}")
```

Now that we have unify (which, remember, makes side-effecty changes using
`make_equal_to`), we can write our infer function. It will look pretty similar
to Algorithm J in overall structure, and in fact our plaintext algorithm
applies just as well.

The main difference is that we invent a new type variable for every AST node
and unify it with some expected type. I don't think this is strictly necessary
(we don't need a type variable to return `IntType` for int literals, for
example[^annotate-phase]), but I think it makes for easier reading. If I were
to slim it down a bit, I think the rule I would use is "only invent a type
variable if it needs to be constrained in the type of something else". Like in
`Apply`.

[^annotate-phase]: I mean, some people even go so far as to split this out into
    its own annotation phase that takes one pass over the AST and adds a type
    variable for each tree node. This is probably helpful for error messages
    and generally keeping type information around for longer.

```python
def infer_j(expr: Object, ctx: Context) -> MonoType:
    result = fresh_tyvar()
    if isinstance(expr, Var):
        scheme = ctx.get(expr.name)
        if scheme is None:
            raise TypeError(f"Unbound variable {expr.name}")
        unify_j(result, scheme.ty)
        return result
    if isinstance(expr, Int):
        unify_j(result, IntType)
        return result
    if isinstance(expr, Function):
        arg_tyvar = fresh_tyvar("a")
        assert isinstance(expr.arg, Var)
        body_ctx = {**ctx, expr.arg.name: Forall([], arg_tyvar)}
        body_ty = infer_j(expr.body, body_ctx)
        unify_j(result, TyCon("->", [arg_tyvar, body_ty]))
        return result
    if isinstance(expr, Apply):
        func_ty = infer_j(expr.func, ctx)
        arg_ty = infer_j(expr.arg, ctx)
        unify_j(func_ty, TyCon("->", [arg_ty, result]))
        return result
    if isinstance(expr, Where):
        name, value, body = expr.binding.name.name, expr.binding.value, expr.body
        value_ty = infer_j(value, ctx)
        body_ty = infer_j(body, {**ctx, name: Forall([], value_ty)})
        unify_j(result, body_ty)
        return result
    raise TypeError(f"Unexpected type {type(expr)}")
```

There you have it. Algorithm J: looks like W, but simpler and (apparently)
faster.

This concludes the section on basic HM. I don't think any in-use language uses
HM like this; they all build on extensions. We have added some of these
extensions to make Scrapscript's type system more expressive.

## Let polymorphism

We alluded to polymorphism earlier because it was already baked into our
implementation (and we had to scratch it out temporarily to write the post),
and we're coming back to it now.

Hindley Milner types also include a `forall` quantifier that allows for some
amount of polymorphism. Consider the function `id = x -> x`. The type of `id`
is `forall 'a. 'a -> 'a`. This is kind of like a lambda for type variables. The
`forall` construct binds type variables like normal lambdas bind normal
variables. Some of the literature calls these *type schemes*.

In order to make inference for polymorphism decidable (I think), you have to
pick some limited set of points in the concrete syntax to generalize types. The
usual place is in `let` bindings. This is why all `let`-bound program variables
(including top-level definitions) are associated with *type schemes* in the
context. I think you could also do it with a `generalize` or `template` keyword
or something, but people tend to use `let` as the signal.

The change to the inference algorithm is as follows:

* if you see a variable `e`,
  * look up the scheme of `e` in the environment
  * **instantiate the scheme and return it**
* if you see a let binding `let n = v in b` (called "where" in scrapscript) `e`,
  * infer the type of the value `v`
  * **generalize `type(v)` to get a scheme `s`**
  * add `n: s` to the environment while type checking the body `b`
  * return `type(b)`

Note that even though we generalize the type to store it into the environment,
we *still return a monotype*.

Generalize is kind of like the opposite of instantiate. It takes a type and
turns it into a scheme using its free variables:

```python
def generalize(ty: MonoType, ctx: Context) -> Forall: ...
```

For example, generalizing `'a` would be `forall 'a. 'a`. Or generalizing `'a
list -> int` would result in `forall 'a. 'a list -> int` (the type scheme of
the list `length` function).

You can't directly use a type scheme, a `Forall`, in a type expression.
Instead, you have to *instantiate* (similar to "call" or "apply") the `Forall`.
This replaces the bound variables ("parameters") with new variables in the
right hand side---in the type. For example, instantiating `forall 'a. 'a -> 'a`
might give you `'t123 -> 't123`, where `'t123` is a fresh variable.

```python
def instantiate(scheme: Forall) -> MonoType: ...
```

Now, to integrate let polymorphism into our Algorithm J inference engine, we
need only change two lines:

```python
def infer_j(expr: Object, ctx: Context) -> MonoType:
    # ...
    if isinstance(expr, Var):
        scheme = ctx.get(expr.name)
        if scheme is None:
            raise TypeError(f"Unbound variable {expr.name}")
        unify_j(result, instantiate(scheme))  # changed!
        return result
    if isinstance(expr, Where):
        name, value, body = expr.binding.name.name, expr.binding.value, expr.body
        value_ty = infer_j(value, ctx)
        value_scheme = generalize(recursive_find(value_ty), ctx)  # changed!
        body_ty = infer_j(body, {**ctx, name: value_scheme})
        unify_j(result, body_ty)
        return result
    # ...
```

Note that due to our union-find implementation, we also need to do this
"recursive find" thing that calls `.find()` recursively to discover all of the
type variables in the type. Otherwise we might just see `'t0` as our only free
type variable or something.

## Algorithm M

Apparently there is a secret third thing that people do, which wasn't formally
proven until 1998 in a paper called [Proofs about a Folklore Let-Polymorphic Type
Inference Algorithm][algorithm-m] (PDF) by Lee and Yi. They call it Algorithm M
because it's a top-down version of Algorithm W (ha ha).

[algorithm-m]: https://www.classes.cs.uchicago.edu/archive/2007/spring/32001-1/papers/p707-lee.pdf

It looks pretty similar to W but there's a third parameter to the inference
function, which is the monotype that you expect the expression to
have[^bidirectional]. We won't have an implementation here, but you should go
take a look at the paper which does a nice side-by-side of W and M. Reader, if
you would like to contribute a small version of Algorithm M using our data
structures, I would be happy to include it.

[^bidirectional]: In that sense it maybe feels a little bit like bidirectional
    type checking, but I also don't know much about that... just going off of
    vibes.

## Extensions for Scrapscript

<!--
Adding type system features that go beyond HM in terms of expressivity often
requires some type annotations from the programmer, or adding significant
complexity to the inference algorithm.
-->

### Recursion

Another quality of life feature that people tend to want in programming
languages, especially programming languages without loops, is recursion. Right
now our infer function won't support functions referring to themselves; we
don't add the function name to the environment when running inference on the
function body.

To add a limited form of recursion, we do the following:

* if typing the pattern `f = FUNCTION` or `f = MATCH_FUNCTION`,
  * then bind `f` to some new type variable to "tie the knot" in
    the context

```python
def infer_j(expr: Object, ctx: Context) -> MonoType:
    # ...
    if isinstance(expr, Where):
        name, value, body = expr.binding.name.name, expr.binding.value, expr.body
        if isinstance(value, Function):
            # Letrec
            func_ty = fresh_tyvar()
            value_ty = infer_j(value, {**ctx, name: Forall([], func_ty)})
        else:
            # Let
            value_ty = infer_j(value, ctx)
        # ...
```

This is helpful, but it's not a full solution. OCaml, for example, has `let
rec`/`and` to write mutually recursive functions. We don't have the syntax to
express that in Scrapscript.

In an ideal world, we would have a way to type mutual recursion anyway. I think
this involves identifying call graphs and strongly connected components within
those graphs. Sounds trickier than it's worth right now[^cfa].

[^cfa]: But control flow analysis (CFA) is on my TODO list anyway, so...

### More datatypes

Scrapscript has lists. While Scrapscript allows for heterogeneous lists
(a list can contain elements of different types at the same time), our type
system will not (at least to start). In order to type these lists, we need to
constrain all the list elements to be the same type when we see a list
constructor.

```python
def infer_j(expr: Object, ctx: Context) -> MonoType:
    # ...
    if isinstance(expr, List):
        list_item_ty = fresh_tyvar()
        for item in expr.items:
            item_ty = infer_j(item, ctx)
            unify_j(list_item_ty, item_ty)
        return TyCon("list", [list_item_ty])
```

This means that an empty list will have type `'a list`. And, interestingly
enough, a `let`-bound empty list will have type scheme `forall 'a. 'a list`.
Note that this is only legal if your lists are immutable, as they are in
Scrapscript.

### Pattern matching

What's the type of a match case pattern? Until a couple of days ago, I didn't
know. Turns out, it's the type that it looks like it should be, as long as you
bind all the variables in the pattern to fresh type variables.

For example, the type of `| [x, y] -> x` is `'a list -> 'a` because the list
constructor tells us this should be a list. But in order to avoid raising
an `Unbound variable` exception when we see `x` in the pattern, we have to
prefill the context with `x` bound to a fresh type variable.

Similarly, the type of `| [x, 5] -> x` is `int list -> int` because the `5`
literal makes the whole thing an `int list`. This means that we gain additional
type information about `x` too!

Let's look at the Python code for inferring a singular match case:

```python
def infer_j(expr: Object, ctx: Context) -> MonoType:
    # ...
    if isinstance(expr, MatchCase):
        pattern_ctx = collect_vars_in_pattern(expr.pattern)
        body_ctx = {**ctx, **pattern_ctx}
        pattern_ty = infer_j(expr.pattern, body_ctx)
        body_ty = infer_j(expr.body, body_ctx)
        unify_j(result, TyCon("->", ppattern_ty, body_ty]))
        return result
```

Then for an entire match function, we unify all of the case functions to make
the pattern types line up and the return types line up.

```python
def infer_j(expr: Object, ctx: Context) -> MonoType:
    # ...
    if isinstance(expr, MatchFunction):
        for case in expr.cases:
            case_ty = infer_j(case, ctx)
            unify_j(result, case_ty)
        return result
```

Similar to typing lists, match patterns have to (for now?) be homogeneous. That
means that the following snippet of code, which is perfectly legal Scrapscript,
wouldn't fly with our type inference:

```
| [x] -> x
| {a=1} -> 2
```

It would be nice to support this but I don't know how right now.

(Also remember to add `MatchFunction` to the type check in the recursive
`let`!)

### Row polymorphism

Scrapscript has records (kind of like structs) and run-time row polymorphism.
This means that you can have a function that pulls out a field from a record
and any record with that field is a legal argument to the function.

See for example two different looking records (2D point and 3D point):

```
get_x left + get_x right
. left  = { x = 1, y = 2 }
. right = { x = 1, y = 2, z = 3 }
. get_x = | { x = x, ... } -> x
```

Hindley Milner doesn't come with support for this right out of the box. If you
add support for records, then you end up with a more rigid system: the records
have to have the same number of fields and same names of fields and same types
of fields. This is safe but overly restrictive.

I think it's possible to "easily" add row polymorphism but we haven't done it
yet. Finding a simple, distilled version of the ideas in the papers has so far
been elusive.

<!--
RowSelect, RowExtend, RowRestrict
-->

* http://www.cs.cmu.edu/~aldrich/courses/819/papers/row-poly.pdf
* https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/scopedlabels.pdf
* https://cs.ioc.ee/tfp-icfp-gpce05/tfp-proc/21num.pdf
* https://github.com/zrho/libra-types/blob/1443ee7e31625a1b9278c29cefb4da044be1c90b/book/src/type_system.md

### Defer-dynamic

Unify doesn't fail but leaves `dyn` and/or run-time check

### Variants

* https://caml.inria.fr/pub/papers/garrigue-polymorphic_variants-ml98.pdf
* https://drops.dagstuhl.de/storage/00lipics/lipics-vol263-ecoop2023/LIPIcs.ECOOP.2023.17/LIPIcs.ECOOP.2023.17.pdf

### Canonicalization or minification of type variables

```python
def minimize(ty: MonoType) -> MonoType:
    # Fingers crossed an expression that we're presenting to the programmer
    # doesn't have more than 26 distinct type variables...
    letters = iter("abcdefghijklmnopqrstuvwxyz")
    free = ftv_ty(ty)
    subst = {ftv: TyVar(next(letters)) for ftv in sorted(free)}
    return apply_ty(ty, subst)
```

### Type-carrying code

Can we make hashes of types?

## Acknowledgements

Thank you to [River Dillon Keefer](https://k-monk.org/) for co-authoring the
code and this post with me at [Recurse Center][rc]. Thank you to the following
fine folks who reviewed the post before it went out:

* [Burak Emir](https://burakemir.ch/)
* [Chris Fallin](https://cfallin.org/)
* [Sinan](https://osa1.net/)

[rc]: https://www.recurse.com/scout/click?t=e8845120d0b98bbc3341fa6fa69539bb

## See also

* Biunification (like CubiML)
* Static Basic Block Versioning
* CFA / [Lambda set defunctionalization](https://dl.acm.org/doi/abs/10.1145/3591260)
  * River's STLC
* https://www.reddit.com/r/ProgrammingLanguages/comments/ijij9o/beyond_hindleymilner_but_keeping_principal_types/
* [More efficient generalization](https://okmij.org/ftp/ML/generalization.html)
* Better error messages with [Wand 1986](https://dl.acm.org/doi/10.1145/512644.512648)
* https://osa1.net/posts/2023-01-23-fast-polymorphic-record-access.html

<!-- Feedback:

1)

- "It would be nice to support this but I don‚Äôt know how right now." I also
  don't know. If you figure it out you should publish about it :-)

- AFAICS the section on generalization doesn't mention the issues with
  variables escaping their binder's scope. Maybe fine for the purposes of this
  blog post.

---

Not saying you should it this way, but I think it would also be interesting to
see a blog post in the style of "you could have invented ...". I think the most
common way of doing any kind of type inference in PLs is you start giving some
abstract type variables to unknown types, then as you see the types used you
add constraints to it (e.g. the type is an `int`, the type is a `list` of some
type, the type must support this operation), then finally to allow polymorphism
you "generalize" the inferred types. All the algorithms you list are ways of
doing this.

Anyway, cool post!


2)

- You could mention that the set of type equality constraints is a like a
  system of equations, that unification is a way to solve them, and that the
  solution is a substitution.
- somewhere you mention records and not knowing how to combine with
  unification, it can be done with subtyping (I have seen that called "semi
  unification" since the equations become inequations)


3)

big picture: might be a style thing / my own writing preferences but it might
be helpful to build up some motivation and intuition at the beginning -- we
jump right in without saying what "unify" means or the intuition about (I
think) slapping labels on everything then starting to constrain them with
equality/merging, and going pairwise structurally down into a tree of type
constructors to do so. right now this reads as a (very thorough)
notes-as-I-implemented-it and a little less as a tutorial, depends on audience
I suppose :-)

some thoughts I jotted down as I read:

- "instead more like function composition" -> give an example where composing
  two maps takes a type from a to b, then b to c?
- constrain/unify: could give some intuition early on about the high-level
  idea? that we're naming everything we can with variables then when we know
  two types "should" be equal we're matching them up structurally? unification
  fundamentally results in knowing more about type vars when a type var unifies
  with something more concrete, right?

-->

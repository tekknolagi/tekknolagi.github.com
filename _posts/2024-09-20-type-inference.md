---
title: Little implementations of the various Damas-Hindley-Milner inference algorithms
layout: post
---

## What is Damas-Hindley-Milner?

A set of rules for discovering types from a program that contains no type
annotations

It has constraints but the constraints give you rewards:

* Something something fast
* Something something principal types
* ???

As a general note: the more constrained your language/system is, the more you
can optimize

On a meta note, we start with Algorithm W because it's the most direct and
doesn't have any "spooky action at a distance" like Algorithm J. It *is*
definitely more visually confusing than Algorithm J, though, so if you get
discouraged, you might want to skip ahead to Algorithm J.

## The data structures

A monotype is a type that maps to a single type

There are two kinds: type variables and type constructors

```python
@dataclasses.dataclass
class MonoType:
    pass


@dataclasses.dataclass
class TyVar(MonoType):
    name: str

    def __str__(self) -> str:
        return f"'{self.name}"


@dataclasses.dataclass
class TyCon(MonoType):
    name: str
    args: list[MonoType]

    def __str__(self) -> str:
        if not self.args:
            return self.name
        if len(self.args) == 1:
            return f"({self.args[0]} {self.name})"
        return f"({self.name.join(map(str, self.args))})"
```

A lot of people make HM type inference implementations by hard-coding functions
and other type constructors like `list` as the only type constructors but we
instead model them all in terms of `TyCon`:

```python
IntType = TyCon("int", [])
BoolType = TyCon("bool", [])
NotFunc = TyCon("->", [BoolType, BoolType])

def list_type(ty: MonoType) -> MonoType:
    return TyCon("list", [ty])
```

Not all Hindley Milner types are expressible in terms of monotypes. Hindley
Milner types also include a `forall` quantifier that allows for some amount of
polymorphism. Consider the function `id = x -> x`. The type of `id` is `forall
'a. 'a -> 'a`. This is kind of like a lambda for type variables. The `forall`
construct binds type variables like normal lambdas bind normal variables. Some
of the literature calls these *type schemes*.

```python
@dataclasses.dataclass
class Forall:
    tyvars: list[TyVar]
    ty: MonoType

    def __str__(self) -> str:
        return f"(forall {', '.join(map(str, self.tyvars))}. {self.ty})"
```

You can't directly use a `Forall` in a type expression. Instead, you have to
*instantiate* ("call", "apply") the `Forall`. This replaces the bound variables
with new variables in the right hand side---in the type. For example,
instantiating `forall 'a. 'a -> 'a` might give you `'t123 -> 't123`.

All of our Hindley Milner implementations will use these three basic data types
to do everything

## Algorithm W

Let's start with Algorithm W. It's probably the most famous one (citation
needed) because it was presented in the paper as the easiest to prove correct.
It's also purely functional, which probably appeals to Haskell nerds.

The idea is that you have a function `infer_w` that takes an expression and an
environment (a "context") and returns a substitution and a type. The
substitution is a mapping from type variables to monotypes and the type is the
type of the expression that you passed in. In Python syntax, that's:

```python
Subst = typing.Mapping[str, MonoType]  # type variable -> monotype
Context = typing.Mapping[str, Forall]  # program variable -> type scheme
def infer_w(expr: Object, ctx: Context) -> tuple[Subst, MonoType]: ...
```

Note that while substitutions map type variables to monotypes,
contexts map source-level variable names to type schemes.

The rules of inference are as follows:

* if you see a function `e`,
  * invent a new type variable for the parameter `t` and add it
    to the environment while type checking the body `b`
  * constrain the type of `e` to be a function from `t` to `type(b)`
  * return `type(e)`
* if you see function application `e`,
  * infer the type of callee `f`
  * infer the type of the argument `a`
  * constrain `type(f)` to be a function from `type(a)` to `type(e)`
  * return `type(e)`
* if you see a variable `e`,
  * look up the scheme of `e` in the environment
  * instantiate the scheme and return it
* if you see a let binding `let n = v in b` (called "where" in scrapscript) `e`,
  * infer the type of the value `v`
  * generalize `type(v)` to get a scheme `s`
  * add `n: s` to the environment while type checking the body `b`
  * return `type(b)`


```python
def infer_w(expr: Object, ctx: Context) -> tuple[Subst, MonoType]:
    if isinstance(expr, Var):
        scheme = ctx.get(expr.name)
        if scheme is None:
            raise TypeError(f"Unbound variable {expr.name}")
        return {}, instantiate(scheme)
    if isinstance(expr, Int):
        return {}, IntType
    if isinstance(expr, Function):
        arg_tyvar = fresh_tyvar()
        assert isinstance(expr.arg, Var)
        body_ctx = {**ctx, expr.arg.name: Forall([], arg_tyvar)}
        body_subst, body_ty = infer_w(expr.body, body_ctx)
        return body_subst, TyCon("->", [apply_ty(arg_tyvar, body_subst), body_ty])
    if isinstance(expr, Apply):
        s1, ty = infer_w(expr.func, ctx)
        s2, p = infer_w(expr.arg, apply_ctx(ctx, s1))
        r = fresh_tyvar()
        s3 = unify_w(apply_ty(ty, s2), TyCon("->", [p, r]))
        return compose(compose(s3, s2), s1), apply_ty(r, s3)
    if isinstance(expr, Where):
        name, value, body = expr.binding.name.name, expr.binding.value, expr.body
        s1, ty1 = infer_w(value, ctx)
        ctx1 = dict(ctx)  # copy
        ctx1.pop(name, None)
        scheme = generalize(ty1, apply_ctx(ctx1, s1))
        ctx2 = {**ctx, name: scheme}
        s2, ty2 = infer_w(body, apply_ctx(ctx2, s1))
        return compose(s2, s1), ty2
    raise TypeError(f"Unexpected type {type(expr)}")
```


## Algorithm M

Pass in a type variable? Annotate the AST?

Top-down (upside-down W, ha ha)

## Algorithm J

Union-find

## Extensions for Scrapscript

### Let polymorphism

### Pattern matching

Binding new variables in pattern arms

Union (?) case functions

### Row polymorphism

RowSelect, RowExtend, RowRestrict

* https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/scopedlabels.pdf

### Defer-dynamic

Unify doesn't fail but leaves `dyn` and/or run-time check

### Variants

* https://drops.dagstuhl.de/storage/00lipics/lipics-vol263-ecoop2023/LIPIcs.ECOOP.2023.17/LIPIcs.ECOOP.2023.17.pdf

### Canonicalization or minification of type variables

## See also

* Biunification (like CubiML)
* Static Basic Block Versioning
* CFA / Lambda set defunctionalization
* https://www.reddit.com/r/ProgrammingLanguages/comments/ijij9o/beyond_hindleymilner_but_keeping_principal_types/
* https://okmij.org/ftp/ML/generalization.html

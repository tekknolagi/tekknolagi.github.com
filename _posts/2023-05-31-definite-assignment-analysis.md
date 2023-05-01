---
layout: post
title: Definite assignment analysis for Python
series: runtime-opt
---

Python is kind of a funky language. It's very flexible about its variable
bindings, which is unusual. I think most of the difference comes from local
variables being function-scoped, where statements like `if` and `while` don't
introduce new scopes.

<!-- TODO -->

In this one, `x` is not defined in the body and assumed to be a global. You can
tell because there is a `LOAD_GLOBAL` in the bytecode.

```python
import dis
def test():
    return x
dis.dis(test)
#   3           0 LOAD_GLOBAL              0 (x)
#               2 RETURN_VALUE
```

In this one, because there is an assignment in the body---even though it is
after the variable use and technically dead code---the compiler infers `x` to
be a local. You can tell because there is a `LOAD_FAST` in the bytecode.

```python
import dis
def test():
  return x
  x = 1
dis.dis(test)
#   3           0 LOAD_FAST                0 (x)
#               2 RETURN_VALUE
```

So what happens if you run it? The compiler did not produce an error when
compiling this code so clearly the error must be handled at run-time---and
hopefully not be a segmentation fault.

```python
def test():
  return x
  x = 1
test()
# Traceback (most recent call last):
#   File "<stdin>", line 4, in <module>
#   File "<stdin>", line 2, in test
# UnboundLocalError: local variable 'x' referenced before assignment
```

Ah, a run-time exception. So there must clearly be some code in the interpreter
that checks, on every variable load, if the variable is defined. Let's peek
inside the interpreter loop. I'm looking at [the code from Python
3.8][py38loop], for reasons that I will explain later
(foreshadowing!)[^later-versions].

[py38loop]: https://github.com/python/cpython/blob/3.8/Python/ceval.c#L1333-L1344
[^later-versions]: It turns out that in mid-2022, Dennis Sweeney [added this
    feature to CPython][cpython-analysis]. This is funny timing, since I
    originally started drafting this code and blog post in March 2022, then
    dropped it until April 2023.

[cpython-analysis]: https://github.com/python/cpython/issues/93143

```c
// Python/ceval.c
PyObject* _PyEval_EvalFrameDefault(PyFrameObject *f, int throwflag) {
// ...
main_loop:
    for (;;) {
    // ...
        switch (opcode) {
        // ...
        case TARGET(LOAD_FAST): {
            PyObject *value = GETLOCAL(oparg);
            if (value == NULL) {
                format_exc_check_arg(tstate, PyExc_UnboundLocalError,
                                     UNBOUNDLOCAL_ERROR_MSG,
                                     PyTuple_GetItem(co->co_varnames, oparg));
                goto error;
            }
            Py_INCREF(value);
            PUSH(value);
            FAST_DISPATCH();
        }
        // ...
        }
    }
    // ...
}
```

Yep. The interpreter reads the local given by the index in `LOAD_FAST`'s oparg
and then checks if it is `NULL`. This would happen if it was uninitialized or
explicitly deleted with `del`.

But most of the time this doesn't happen. Most of the time people don't write
silly code like this. It's not every day that you boot up some Python code and
hit this error. So we should be able to optimize for the common case: the
variables are initialized.

This is easy enough with straight-line code like this Fibonacci example from
[python.org](https://www.python.org/):

```python
def fib(n):
    a, b = 0, 1
    while a < n:
        print(a, end=' ')
        a, b = b, a+b
    print()
```

The variables `a` and `b` are initialized at the top and never deleted. They
are reassigned, sure, but they always are bound to some value. But it's not
always so obvious when a variable is defined. Sometimes the initialization
depends on some invariant in your code that you know about but have a hard time
demonstrating to the compiler. Something like this, perhaps:

```python
def foo(cond):
    if cond:
        x = 123
    # ...
    if cond:
        print(x)
```

But most code isn't like that. Most code is pretty reasonable. So let's tackle
that.

## Pre-reading

If you are not already familiar with control-flow graphs, I recommend reading a
previous blog post, [Discovering basic
blocks](/blog/discovering-basic-blocks/), that builds a simple CFG from Python
bytecode. While we won't be using that exact data structure, we will be using
something very similar.

## Definite assignment analysis

<!--

Maybe you only need a local in some cases of your function. It's a bit gross,
but I have seen this kind of code before:

```python
def find_spec(self, fullname, target=None):
    is_namespace = False
    # ...
    if cache_module in cache:
        base_path = _path_join(self.path, tail_module)
        for suffix, loader_class in self._loaders:
            # ...
        else:
            is_namespace = _path_isdir(base_path)
    # ...
    if is_namespace:
        # ...
        spec.submodule_search_locations = [base_path]
    # ...
```

See how `base_path` is only ever defined if the module is in the cache, and
it's only ever read if `is_namespace` is truthy? There's no way a compiler
would be able to reason about that.

Even a simpler case like the following, where the function is kind of split
into two cases, is tricky:

```python
def foo(cond):
    if cond:
        x = 123
    # ...
    if cond:
        print(x)
```

It's often more like this
example, where we have a

```python
def test(cond):
  if cond:
    x = 3
  else:
    x = 4
  return x
```
-->
<!-- TODO compare against CPython implementation in upstream -->

---
title: "Mining type information for faster Python C extensions"
layout: post
date: 2023-12-15
---

## intro

Python is neat

In order to work with existing C libraries (and also for performance reasons),
Python has a C API. This C API goes two ways:

* Applications can start up Python and control it from C
* Python can load C libraries and call into them

<!--
This works great when you are inside the main Python implementation, CPython,
and your API is 
-->

Python has two major implementations: CPython and PyPy. For CPython, the C API
is largely functions that already exist and are used for normal runtime stuff.
For PyPy, it's different

PyPy has its own object model, runtime, and moving garbage collector. This is
all to get better performance. Unfortunately, this means that whenever you call
a C API function from PyPy, it has to stop what it's doing, set up some C API
scaffolding, do the C API call, and then take down the scaffolding

<!-- insert diagram like pyro's -->

For example, PyPy has no `PyObject*`. It has to allocate a `PyObject`, make it
point into the PyPy heap, call the C API function, and then (potentially) free
the `PyObject`. Worse, the C API function may not even *need* the `PyObject` to
exist.

A lot of C API functions are structured like

```c
long foo_impl(long num) {
    return num * 2;
}

PyObject* foo(PyObject* obj) {
    long num = PyLong_AsLong(obj);
    if (num < 0 && PyErr_Occurred()) {
        return NULL;
    }
    long result = foo_impl(num);
    return PyLong_FromLong(result);
}
```

In this example, the `PyObject*` code is only a wrapper around another function
that works directly on C integers

So even if the PyPy JIT is doing great work and has eliminated memory
allocation in Python code---PyPy could have unboxed some heap allocated int
into a C int--- it still has to allocate a PyPy object, then a
`PyObject*` ... only to throw both away soon after.

<figure style="display: block; margin: 0 auto;">
  <object class="svg" type="image/svg+xml" data="https://mermaid.ink/svg/pako:eNqVkV1LwzAUhv9KOJdSR2s_0uZioO5m3ihMEaQ3sT3d6pqcmqbMOfbfTTsnTkEwV0nO83KenOygoBJBQIevPeoCZ7VcGqlyzdzSZJE1WFlGFbuZ3wu2IIXsbmtXpNmQPHCudD6dXrNHI9sWjWCXTUOFtAN6-_yChT07gF_IiM9V26BCbaWtSQv2oDeu-jMzSph6uRotfqdmxLrBakNmfexyyvzfbXzrHzonM7mSxZpZ-j4W8EChUbIu3Wh3QzAHu3JGOQi3LbGSfWNzyPXeobK3tNjqAoQ1PXrQt6Xz-_wJEJVsOnfbSv1EpI6QO4LYwRuIkAeTIAq5zy94HGZ-4sEWBPcnQRpynqRxHCUpj_cevI95f5JkWRRGfhDGQRSlPNh_AK9DpdY">
  </object>
  <figcaption>Fig. 1 - something</figcaption>
</figure>
<!--
https://mermaid.live/edit#pako:eNqVkV1LwzAUhv9KOJdSR2s_0uZioO5m3ihMEaQ3sT3d6pqcmqbMOfbfTTsnTkEwV0nO83KenOygoBJBQIevPeoCZ7VcGqlyzdzSZJE1WFlGFbuZ3wu2IIXsbmtXpNmQPHCudD6dXrNHI9sWjWCXTUOFtAN6-_yChT07gF_IiM9V26BCbaWtSQv2oDeu-jMzSph6uRotfqdmxLrBakNmfexyyvzfbXzrHzonM7mSxZpZ-j4W8EChUbIu3Wh3QzAHu3JGOQi3LbGSfWNzyPXeobK3tNjqAoQ1PXrQt6Xz-_wJEJVsOnfbSv1EpI6QO4LYwRuIkAeTIAq5zy94HGZ-4sEWBPcnQRpynqRxHCUpj_cevI95f5JkWRRGfhDGQRSlPNh_AK9DpdY
-->
<!--
sequenceDiagram
    note left of JIT: Some Python code
    JIT->>C Wrapper: Allocate PyObject*
    C Wrapper->>C Implementation: Unwrap PyObject*
    note right of C Implementation: Do some work
    C Implementation->>C Wrapper: Allocate PyObject*
    C Wrapper->>JIT: Unwrap PyObject*
    note left of JIT: Back to Python code
-->

If there was a way to communicate that `foo` expects an `int` and is going to
unbox it into a C int (and will also reutrn a C `int`) to PyPy, it wouldn't
need to do any of these shenanigans

proposal: <!-- TODO -->

diagram: <!-- TODO -->

And yes, ideally there wouldn't be a C API call at all. But sometimes you have
to, and you might as well speed up that call

## Potsdam

I talked to the authors of PyPy and GraalPython over some coffee at ECOOP 2022.
They've been working on a project called HPy

https://github.com/hpyproject/hpy/issues/129

I decided to take a stab at implementing it for Cinder

https://github.com/faster-cpython/ideas/issues/546

## Sketchy C things

## Implementing in PyPy

## Where do all the C extensions come from?

None in PyPy standard library since they are all implemented in Python

### Cython

In this snippet of Cython code, we make a function that adds two machine
integers.

```cython
cpdef int add(int a, int b):
    return a + b
```

Cython will generate a very fast C function that adds two machine integers.
Calls to this from Cython are type checked at compile time and will be as fast
as your C compiler allows:

```c
static int add(int __pyx_v_a, int __pyx_v_b) {
  return __pyx_v_a + __pyx_v_b;
}
```

Since we used `cpdef` instead of `cdef`, Cython will also generate a wrapper C
extension function so that this function can be called from Python.

This means that the generated Cython code looks like (a much uglier version of)
below. **You don't need to understand or really even read the big blob** of
cleaned-up generated code below. You just need to say "ooh" and "aah" and "wow,
so many if-statements and so much allocation and so many function calls."


<!-- NOTE: this is worse, even, since it's unwrapping fastcall too -->

```c
static PyObject *add_and_box(CYTHON_UNUSED PyObject *__pyx_self,
                             int __pyx_v_a,
                             int __pyx_v_b) {
  int result = add(__pyx_v_a, __pyx_v_b, 0);
  // Check if an error occurred (unnecessary in this case)
  if (result == ((int)-1) && PyErr_Occurred()) {
    return NULL;
  }
  // Wrap result in PyObject*
  return PyLong_FromLong(result);
}

static PyObject *add_python(PyObject *__pyx_self,
                            PyObject *const *__pyx_args,
                            Py_ssize_t __pyx_nargs,
                            PyObject *__pyx_kwds) {
  // Check how many arguments were given
  PyObject* values[2] = {0,0};
  if (__pyx_nargs == 2) {
    values[1] = __pyx_args[1];
    values[0] = __pyx_args[0];
  } else if (__pyx_nargs == 1) {
    values[0] = __pyx_args[0];
  }
  // Check if any keyword arguments were given
  Py_ssize_t kw_args = __Pyx_NumKwargs_FASTCALL(__pyx_kwds);
  // Match up mix of position/keyword args to parameters
  if (__pyx_nargs == 0) {
    // ...
  } else if (__pyx_nargs == 1) {
    // ...
  } else if (__pyx_nargs == 2) {
    // ...
  } else {
    // ...
  }
  // Unwrap PyObject* args into C int
  int __pyx_v_a = PyLong_AsLong(values[0]);
  // Check for error (unnecessary if we know it's an int)
  if ((__pyx_v_a == (int)-1) && PyErr_Occurred()) {
    return NULL;
  }
  int __pyx_v_b = PyLong_AsLong(values[1]);
  // Check for error (unnecessary if we know it's an int)
  if ((__pyx_v_b == (int)-1) && PyErr_Occurred()) {
    return NULL;
  }
  // Call generated C implementation of add
  return add_and_box(__pyx_self, __pyx_v_a, __pyx_v_b);
}
```

Now, to be clear: this is probably the fastest thing possible for interfacing
with CPython. Cython has been worked on for years and years and it's *very*
fast. But we have some other runtimes that have different performance
characeteristics

### Other binding generators

pybind11, nanobind, ...

Even Argument Clinic in CPython

### Hand-written

## Small useless benchmark

```c
#include <Python.h>

long inc_impl(long arg) {
  return arg+1;
}

PyObject* inc(PyObject* module, PyObject* obj) {
  (void)module;
  long obj_int = PyLong_AsLong(obj);
  if (obj_int == -1 && PyErr_Occurred()) {
    return NULL;
  }
  long result = inc_impl(obj_int);
  return PyLong_FromLong(result);
}

PyPyTypedMethodMetadata inc_sig = {
  .arg_type = T_C_LONG,
  .ret_type = T_C_LONG,
  .underlying_func = inc_impl,
  .ml_name = "inc",
};

static PyMethodDef mytypedmod_methods[] = {
    // TODO(max): Add METH_FASTCALL | METH_TYPED
    {inc_sig.ml_name, inc, METH_O | METH_TYPED, "Add one to an int"},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef mytypedmod_definition = {
    PyModuleDef_HEAD_INIT, "mytypedmod",
    "A C extension module with type information exposed.", -1,
    mytypedmod_methods,
    NULL,
    NULL,
    NULL,
    NULL};

PyMODINIT_FUNC PyInit_mytypedmod(void) {
  PyObject* m = PyState_FindModule(&mytypedmod_definition);
  if (m != NULL) {
    Py_INCREF(m);
    return m;
  }
  return PyModule_Create(&mytypedmod_definition);
}
```

```python
# bench.py
import mytypedmod


def bench():
    result = 0
    inc = mytypedmod.inc
    for i in range(1_000_000):
        result += inc(10)
    return result


bench()
```

```console
$ pyp3 setup.py build
$ pypy3 bench.py
```

## Looking forward

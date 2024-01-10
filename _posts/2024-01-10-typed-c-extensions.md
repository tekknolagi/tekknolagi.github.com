---
title: "Mining type information for faster Python C extensions"
layout: post
date: 2024-01-10
---

## intro

Python is pretty widely-used. For years, CPython was the only implementation,
and CPython was not designed to be fast. The Python community needed some
programs to go faster and determined that the best path forward was to write
some modules in C and interact with them from Python. Worked just fine.

Then other Python runtimes like PyPy came along. PyPy's execution of normal
code is *very* fast, at least until it hits a call into a function written in
C.

PyPy has its own object model, runtime, and moving garbage collector. This is
all to get better performance. Unfortunately, this means that whenever you call
a C API function from PyPy, it has to stop what it's doing, set up some C API
scaffolding, do the C API call, and then take down the scaffolding.

For example, the C API is centered around `PyObject` pointers. PyPy does not
normally use `PyObject`s. It has to allocate a `PyObject`, make it point into
the PyPy heap, call the C API function, and then (potentially) free the
`PyObject`.

<figure style="display: block; margin: 0 auto;">
    <object class="svg" type="image/svg+xml" data="https://mermaid.ink/svg/pako:eNptUMtOhTAQ_ZVmlgYJyKPQxU3Uu9GNJldjYthUGB5KWywlVyT8uwUfQWNX0zPnnDkzE-SqQGDQ4-uAMsd9wyvNRSaJfVIZJC2WhqiSXF_dMXJQAsntaGolyaL85NnW6W53SR407zrUjJy3rcq5Wag3T8-Ym5ONoW6qenXcCPaK9Nba1I2syLEx9V_lD9cOWpPcy6MF_h3wK_EFz1-IUdvQ4IBALXhT2MWnRZiBqVFgBsyWBZZ8aE0GmZwtlQ9GHUaZAzN6QAeGrrCbfd0JWMnb3qIdl49KiW-S_QKb4A1YQH3XDwPq0TMaBakXOzACo57rJwGlcRJFYZzQaHbgfdV7bpymYRB66QInaUznD-SXhKM">
    </object>
  <figcaption>Fig. 1 - something</figcaption>
</figure>
<!--
sequenceDiagram
    note left of JIT: Some Python code
    JIT->>C Wrapper: Allocate PyObject*
    note right of C Wrapper: Do something with PyObject*
    C Wrapper->>JIT: Unwrap PyObject*
    note left of JIT: Back to Python code
-->
<!-- https://mermaid.live/edit#pako:eNptUMtOhTAQ_ZVmlgYJyKPQxU3Uu9GNJldjYthUGB5KWywlVyT8uwUfQWNX0zPnnDkzE-SqQGDQ4-uAMsd9wyvNRSaJfVIZJC2WhqiSXF_dMXJQAsntaGolyaL85NnW6W53SR407zrUjJy3rcq5Wag3T8-Ym5ONoW6qenXcCPaK9Nba1I2syLEx9V_lD9cOWpPcy6MF_h3wK_EFz1-IUdvQ4IBALXhT2MWnRZiBqVFgBsyWBZZ8aE0GmZwtlQ9GHUaZAzN6QAeGrrCbfd0JWMnb3qIdl49KiW-S_QKb4A1YQH3XDwPq0TMaBakXOzACo57rJwGlcRJFYZzQaHbgfdV7bpymYRB66QInaUznD-SXhKM -->

That's a lot of overhead. (And there's more, too. See Antonio Cuni's [excellent
blog
post](https://www.pypy.org/posts/2018/09/inside-cpyext-why-emulating-cpython-c-8083064623681286567.html).

Worse, the C API function may not even *need* the `PyObject` to exist in the
first place. A lot of C API functions are structured like:

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
that works directly on C integers.

<figure style="display: block; margin: 0 auto;">
  <object class="svg" type="image/svg+xml" data="https://mermaid.ink/svg/pako:eNqVkV1LwzAUhv9KOJdSR2s_0uZioO5m3ihMEaQ3sT3d6pqcmqbMOfbfTTsnTkEwV0nO83KenOygoBJBQIevPeoCZ7VcGqlyzdzSZJE1WFlGFbuZ3wu2IIXsbmtXpNmQPHCudD6dXrNHI9sWjWCXTUOFtAN6-_yChT07gF_IiM9V26BCbaWtSQv2oDeu-jMzSph6uRotfqdmxLrBakNmfexyyvzfbXzrHzonM7mSxZpZ-j4W8EChUbIu3Wh3QzAHu3JGOQi3LbGSfWNzyPXeobK3tNjqAoQ1PXrQt6Xz-_wJEJVsOnfbSv1EpI6QO4LYwRuIkAeTIAq5zy94HGZ-4sEWBPcnQRpynqRxHCUpj_cevI95f5JkWRRGfhDGQRSlPNh_AK9DpdY">
  </object>
  <figcaption>Fig. 2 - something</figcaption>
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

All of the bits in the middle between the JIT and the C implementation are
"wasted work" because it's not needed for the actual execution of the user's
program.

So even if the PyPy JIT is doing great work and has eliminated memory
allocation in Python code---PyPy could have unboxed some heap allocated int
into a C int---it still has to heap allocate a `PyObject*` ... only to throw
both away soon after.

If there was a way to communicate that `foo` expects an `int` and is going to
unbox it into a C `int` (and will also reutrn a C `int`) to PyPy, it wouldn't
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

The existing `PyMethodDef`

```c
// Old stuff
struct PyMethodDef {
    const char  *ml_name;   /* The name of the built-in function/method */
    PyCFunction  ml_meth;   /* The C function that implements it */
    int          ml_flags;  /* Combination of METH_xxx flags, which mostly
                               describe the args expected by the C func */
    const char  *ml_doc;    /* The __doc__ attribute, or NULL */
};
typedef struct PyMethodDef PyMethodDef;
```

We want to store this kind of metadata

```c
struct PyPyTypedMethodMetadata {
  int arg_type;
  int ret_type;
  void* underlying_func;
  const char ml_name[100];
};
typedef struct PyPyTypedMethodMetadata PyPyTypedMethodMetadata;
```

(note that this only stores type information for one arg, but that can be
extended easily enough)

ABI changes are a no-no

What to do?

```c
// New stuff
struct PyPyTypedMethodMetadata {
  int arg_type;
  int ret_type;
  void* underlying_func;
  const char ml_name[100];
};
typedef struct PyPyTypedMethodMetadata PyPyTypedMethodMetadata;

PyPyTypedMethodMetadata*
GetTypedSignature(PyMethodDef* def)
{
  assert(def->ml_flags & METH_TYPED);
  return (PyPyTypedMethodMetadata*)(def->ml_name - offsetof(PyPyTypedMethodMetadata, ml_name));
}
```

<svg version="1.1" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 618.5 126.5" width="618.5" height="126.5">
  <!-- svg-source:excalidraw -->
  
  <defs>
    <style class="style-fonts">
      @font-face {
        font-family: "Virgil";
        src: url("https://excalidraw.com/Virgil.woff2");
      }
      @font-face {
        font-family: "Cascadia";
        src: url("https://excalidraw.com/Cascadia.woff2");
      }
      @font-face {
        font-family: "Assistant";
        src: url("https://excalidraw.com/Assistant-Regular.woff2");
      }
    </style>
    
  </defs>
  <rect x="0" y="0" width="618.5" height="126.5" fill="#ffffff"></rect><g stroke-linecap="round" transform="translate(10 10) rotate(0 216 48.5)"><path d="M24.25 0 C137.76 0.74, 252.66 1.25, 407.75 0 M24.25 0 C133.75 -1.6, 242.8 -2.49, 407.75 0 M407.75 0 C422.22 -0.06, 432.41 7.86, 432 24.25 M407.75 0 C424.83 1.55, 431.62 10.25, 432 24.25 M432 24.25 C431.26 36, 431.05 45.37, 432 72.75 M432 24.25 C431.31 37.25, 430.98 51.86, 432 72.75 M432 72.75 C433.23 88.41, 421.98 96.89, 407.75 97 M432 72.75 C430.17 90.62, 424.32 96.24, 407.75 97 M407.75 97 C313.85 98.48, 218.96 96.58, 24.25 97 M407.75 97 C309.98 97.32, 211.48 97.13, 24.25 97 M24.25 97 C7.8 98.52, -1.82 89.67, 0 72.75 M24.25 97 C7.43 95.29, -2.16 88.95, 0 72.75 M0 72.75 C0.75 59.9, -1.33 43.31, 0 24.25 M0 72.75 C-0.42 55.39, 0.78 37.15, 0 24.25 M0 24.25 C1.44 9.34, 8.8 1.32, 24.25 0 M0 24.25 C-0.4 8.7, 5.97 0.7, 24.25 0" stroke="#000000" stroke-width="1" fill="none"></path></g><g stroke-linecap="round" transform="translate(324.5 19.5) rotate(0 142 48.5)"><path d="M24.25 0 C96.98 0.07, 170.08 1.31, 259.75 0 M24.25 0 C91.64 1.01, 159.28 0.63, 259.75 0 M259.75 0 C277.18 -1.81, 282.61 8.34, 284 24.25 M259.75 0 C277.04 0.75, 286.19 8.14, 284 24.25 M284 24.25 C283.22 35.88, 284.64 52.64, 284 72.75 M284 24.25 C285.26 35.92, 284.01 49.77, 284 72.75 M284 72.75 C283.04 89.11, 277.03 98.59, 259.75 97 M284 72.75 C282.59 88.86, 276.09 98.31, 259.75 97 M259.75 97 C202.55 96.59, 145.75 96, 24.25 97 M259.75 97 C194.6 98.64, 130.77 99.23, 24.25 97 M24.25 97 C7.55 96.82, -0.08 87.92, 0 72.75 M24.25 97 C8.82 95.01, 0.75 90.45, 0 72.75 M0 72.75 C0.42 59.62, -0.5 44.56, 0 24.25 M0 72.75 C-0.31 62, 0.39 50.58, 0 24.25 M0 24.25 C0.6 7.93, 7.62 0.36, 24.25 0 M0 24.25 C-1.19 5.9, 7.1 1.56, 24.25 0" stroke="#000000" stroke-width="1" fill="none"></path></g><g transform="translate(35 48.375) rotate(0 136.49166870117188 12.5)"><text x="0" y="0" font-family="Virgil, Segoe UI Emoji" font-size="20px" fill="#000000" text-anchor="start" style="white-space: pre;" direction="ltr" dominant-baseline="text-before-edge">PyPyTypedMethodMetadata</text></g><g transform="translate(459 53.125) rotate(0 64.375 12.5)"><text x="0" y="0" font-family="Virgil, Segoe UI Emoji" font-size="20px" fill="#000000" text-anchor="start" style="white-space: pre;" direction="ltr" dominant-baseline="text-before-edge">PyMethodDef</text></g><g transform="translate(345 50.75) rotate(0 40.358333587646484 12.5)"><text x="0" y="0" font-family="Virgil, Segoe UI Emoji" font-size="20px" fill="#000000" text-anchor="start" style="white-space: pre;" direction="ltr" dominant-baseline="text-before-edge">ml_name</text></g></svg>

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

static PyMethodDef mytypedmod_methods[] = {
    {"inc", inc, METH_O, "Add one to an int"},
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

```c
// TODO(max): Turn this into a diff?
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


def main():
    i = 0
    while i < 10_000_000:
        i = mytypedmod.inc(i)
    return i


if __name__ == "__main__":
    print(main())
```

```console
$ python3.10 bench.py

$ pypy3.10 setup.py build
$ pypy3.10 bench.py

$ pypy3.10-new setup.py build
$ pypy3.10-new bench.py
```

perf measured with hyperfine:

CPython3.10 846.6ms
PyPy3.10nightly 2.269s
PyPy3.10patched 168.1ms

## Profiling large applications

IG workload (for example)

Use of Static Python as binding generator

## Approaches in other runtimes

PyPy: differences from specifying C binding *in Python*

JS: not much extension in the browser, just the shell
JSC has DOMJIT
V8 has Torque

Ruby: ???

Java: ???

Hack Native: See if I can get details about this from Edwin

## Looking forward

### Motivation for CPython to include

### Motivation for HPy to include

### Implementing in binding generators

### Typed dialects?

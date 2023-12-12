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

<figure style="display: block; margin: 0 auto; max-width: 400px;">
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

### Other binding generators

## Small useless benchmark

## Looking forward

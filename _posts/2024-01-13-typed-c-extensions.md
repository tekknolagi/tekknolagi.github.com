---
title: "Type information for faster Python C extensions"
layout: post
date: 2024-01-13
---

**Update:** The paper version of this post is accepted at PLDI SOAP 2024. Take
a look at the [preprint](/assets/img/dr-wenowdis.pdf) (PDF).

PyPy is an alternative implementation of the Python language. PyPy's C API
compatibility layer has some performance issues. [CF
Bolz-Tereick](https://cfbolz.de/) and I are working on a way to make PyPy's C
API interactions much faster. It's looking very promising. Here's a sketch of
how it works.

## The C API as lingua franca

Python is pretty widely-used. For years, CPython was the only implementation,
and CPython was not designed to be fast. The Python community needed some
programs to go faster and determined that the best path forward was to write
some modules in C and interact with them from Python. Worked just fine.

Then other Python runtimes like PyPy came along. PyPy includes a JIT compiler
and its execution of normal Python code is *very* fast, at least until it hits
a call from Python to a C extension function. Then things go a little bit
sideways. First and foremost because the PyPy JIT can't "see into" the native
code; it's generated outside of the JIT by some other compiler and is therefore
opaque. And second of all because the binding API for the aforementioned C
modules ("The C API") requires a totally different object and memory
representation than PyPy has internally.

PyPy has its own object model, runtime, and moving garbage collector, all to
get better performance. Unfortunately, this means that whenever you call a C
API function from PyPy, it has to stop what it's doing, set up some C API
scaffolding, do the C API call, and then take down the scaffolding.

For example, the C API is centered around `PyObject` pointers. PyPy does not
use `PyObject`s in the normal execution of Python code. When it interacts with
C API code, it has to allocate a `PyObject`, make it point into the PyPy heap,
call the C API function, and then (potentially) free the `PyObject`. (This
ignores GIL stuff and exception checking, which is also an issue.)

<figure style="display: block; margin: 0 auto;">
    <object class="svg" type="image/svg+xml" data="/assets/img/python-capi-box.svg">
    </object>
  <figcaption markdown="1">
  Fig. 1 - C extensions deal in `PyObject*` so any runtime that wants to
  interface with them has to also deal in `PyObject*`.
  </figcaption>
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
post](https://www.pypy.org/posts/2018/09/inside-cpyext-why-emulating-cpython-c-8083064623681286567.html).)
And it's a hard problem that has bitten multiple alternative Python
runtimes[^capi-problem].

[^capi-problem]: This C API problem has bitten pretty much every alternative
    runtime to CPython. They all try changing something about the object model
    for performance and then they all inevitably hit the problem of pervasive C
    API modules in the wild. This is part of what hurt the
    [Skybison](https://github.com/tekknolagi/skybison) project.

In addition to the overhead of boxing into a `PyObject`, the underlying C
function that the C extension calls may not even *need* the `PyObject` to exist
in the first place. For example, a lot of C API functions are structured like
this:

```c
long inc_impl(long num) {
    return num + 1;
}

PyObject* inc(PyObject* obj) {
    long num = PyLong_AsLong(obj);
    if (num < 0 && PyErr_Occurred()) {
        return NULL;
    }
    long result = inc_impl(num);
    return PyLong_FromLong(result);
}
```

In this example, the `PyObject*` code `inc` is only a wrapper around another
function `inc_impl` that works directly on C integers.

<figure style="display: block; margin: 0 auto;">
  <object class="svg" type="image/svg+xml" data="/assets/img/python-capi-box-unbox.svg">
  </object>
  <figcaption markdown="1">
  Fig. 2 - The runtimes still have to manufacture `PyObject*` even if the
  underlying C code doesn't know anything about Python. The unboxing is an
  another source of overhead, unfortunately.
  </figcaption>
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

All of the bits in the middle between the JIT and the C implementation (the
entire `inc` function, really) are "wasted work" because the work is not needed
for the actual execution of the user's program.

So even if the PyPy JIT is doing great work and has eliminated memory
allocation in Python code---PyPy could have unboxed some heap allocated Python
integer object into a C long, for example---it still has to heap allocate a
`PyObject` for the C API... only to throw it away soon after.

If there was a way to communicate to PyPy that `inc` expects a `long` and is
going to unbox it into a C `long` (and will also return a C `long`), it
wouldn't need to do any of these shenanigans.

And yes, ideally there wouldn't be a C API call at all. But sometimes you have
to (perhaps because you have no control over the code), and you might as well
speed up that call. Let's see if it can be done.

## Potsdam

This is where I come into this. I was at the ECOOP conference in 2022 where
[CF](https://cfbolz.de/) introduced me to some other implementors
of alternative Python runtimes. I got to talk to the authors of PyPy and ZipPy
and GraalPython over some coffee and beer. They're really nice.

They've been collectively working on a project called HPy. HPy is a new design
for a C API Python that takes alternative runtimes into account. As part of
this design, they were [investigating a way to pipe type
information](https://github.com/hpyproject/hpy/issues/129) from C extension
modules through the C API and into a place where the host runtime can read it.

It's a tricky problem because not only is there a C API, but also a C ABI (note
the "B" for "binary"). While an API is an abstract contract between caller and
callee for how to call a function, an ABI is more concrete. In the case of the
C ABI, it means not changing struct layouts or sizes, adding function
parameters, things like that. This is kind of a tight constraint and it wasn't
clear what the best backward-compatible way to add type information was.

Sometime either in this meeting or shortly after, I had an idea for how to do
it without changing the API or ABI and I decided to take a stab at implementing
it for [Cinder](https://github.com/facebookincubator/cinder/) (the Python
runtime I was working on at the time).

## The solution: sketchy C things?

In order to better understand the problems, let's take a look at the kind of
type information we want to add. This is the kind of type metadata we want to
add to each typed method, represented as a C struct.

```c
struct PyPyTypedMethodMetadata {
  int arg_type;
  int ret_type;
  void* underlying_func;
};
typedef struct PyPyTypedMethodMetadata PyPyTypedMethodMetadata;
```

In this artificially limited example, we store the type information for one
argument (but more could be added in the future), the type information for the
return value, and the underlying (non-`PyObject*`) C function pointer.

But it's not clear where to put that in a `PyMethodDef`. The existing
`PyMethodDef` struct looks like this. It contains a little bit of metadata and
a C function pointer (the `PyObject*` one). In an ideal world, we would "just"
add the type metadata to this struct and be done with it. But we can't change
its size for ABI reasons.

```c
struct PyMethodDef {
    const char  *ml_name;   /* The name of the built-in function/method */
    PyCFunction  ml_meth;   /* The C function that implements it */
    int          ml_flags;  /* Combination of METH_xxx flags, which mostly
                               describe the args expected by the C func */
    const char  *ml_doc;    /* The __doc__ attribute, or NULL */
};
typedef struct PyMethodDef PyMethodDef;
```

What to do? Well, I decided to get a little weird with it and see if we could
sneak in a pointer to the metadata somehow. My original idea was to put the
entire `PyPyTypedMethodMetadata` struct *behind* the `PyMethodDef` struct (kind
of like how `malloc` works), but that wouldn't work so well: `PyMethodDef`s are
commonly statically allocated in arrays, and we can't change the layout of
those arrays.

But what we *can* do is point the `ml_name` field to a buffer inside another
struct[^linux-trick].

[^linux-trick]: I later learned that this is common in the Linux kernel.

Then, when we notice that a method is marked as typed (with a new `METH_TYPED`
flag we can add to the `ml_flags` bitset), we can read backwards to find the
`PyPyTypedMethodMetadata` struct. Here's how you might do that in C:

```c
struct PyPyTypedMethodMetadata {
  int arg_type;
  int ret_type;
  void* underlying_func;
  const char ml_name[100];  // New field!
};
typedef struct PyPyTypedMethodMetadata PyPyTypedMethodMetadata;

PyPyTypedMethodMetadata*
GetTypedSignature(PyMethodDef* def)
{
  assert(def->ml_flags & METH_TYPED);  // A new type of flag!
  return (PyPyTypedMethodMetadata*)(def->ml_name - offsetof(PyPyTypedMethodMetadata, ml_name));
}
```

And here's a diagram to illustrate this because it's really weird and
confusing.

<figure style="display: block; margin: 0 auto;">
  <object class="svg" type="image/svg+xml" data="/assets/img/python-capi-type-metadata.svg">
  </object>
</figure>

I started off with a [mock
implementation](https://gist.github.com/tekknolagi/f4acd2202f6448d4a5813a43eda90121)
of this in C (no Python C API, just fake structures to sketch it out) and it
worked. So I implemented a hacky version of it in Cinder, but never shipped it
because my integration with Cinder was a little too hacky. I [wrote up the
ideas](https://github.com/faster-cpython/ideas/issues/546) for posterity in
case someone wanted to take up the project.

A year later, nobody else had, so I decided to poke CF and see if
we could implement it in PyPy. We'll see how that implementation looks in a
minute. But first, an aside on where C extensions come from.

## Where do all the C extensions come from?

Well, in PyPy, there are none in the standard library. PyPy has been almost
entirely written in Python so that the code is visible to the JIT. But people
like using Python packages, and some Python packages contain C extensions.

There are a couple of different ways to write a C extension. The "simplest" (as
in, all the components are visible and there is no magic and there are no
external dependencies) is to hand-write it. If you don't want to do that, you
can also use a binding generator to write the glue code for you. I have the
most experience with Cython, but other binding generators like nanobind,
pybind11, and even CPython's own Argument Clinic exist too!

### Hand-written

Let's recall the `inc`/`inc_impl` function from earlier. That's a reasonable
example of a function that could be integrated as a hand-written C extension to
Python. In order to make it callable from Python, we have to make a full C
extension module. In this case, that's just a list of function pointers and how
to call them.

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

We have an array of `PyMethodDef` structs, one for each method we want to wrap.
Then we have a `PyModuleDef` to define the module, which can also contain
attributes and some other stuff. Then we provide a sort of `__new__` function
for the module, in the form of a `PyInit_` function. This is found by `dlopen`
in the C extension loader built into Python.

It's possible to manually augment this module by adding a
`PyPyTypedMethodMetadata` struct and a `METH_TYPED` flag. It's a little
cumbersome, but if it speeds up interactions with the module... well, extension
authors might be cajoled into adding the type information or at least accepting
a pull request.

But not all extensions are hand-written. Many are generated by binding
generators like Cython. And Cython is interesting because it can generate the
type signatures automatically...

### Cython

Unlike many other binding generators for Python, Cython provides a
fully-featured Python-like programming language that compiles to C. The types
obey different rules than in normal Python code and can be used for
optimization. Cython also has primitive types. Let's see an example.

In this snippet of Cython code, we make a function that adds two machine
integers and returns a machine integer.

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

This means that the generated Cython wrapper code looks like (a much uglier
version of) below. **You don't need to understand or really even read** the big
blob of cleaned-up and annotated generated code below. You just need to say
"ooh" and "aah" and "wow, so many if-statements and so much allocation and so
many function calls."

And it's also a little worse than the `METH_O` example above since it has to
unwrap an array of fastcall args and do some argument processing.

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
fast. But CPython isn't the only runtime in town and the other runtimes have
different performance characteristics, as we explored above.

Since so many C extension are generated with Cython, there's a big opportunity:
if we manage to get the Cython compiler to emit typed metadata for the
functions it compiles, those functions could become *much* faster under
runtimes such as PyPy.

In order to justify such a code change, we have to see how much faster the
typed metadata makes things. So let's benchmark.

## A small, useless benchmark

Let's try benchmarking the interpreter interaction with the native module with
a silly benchmark. It's a little silly because it's not super common (in use
cases I am familiar with anyway) to call C code in a hot loop like this without
writing the loop in C as well. But it'll be a good reference for the maximum
amount of performance we can win back.

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

We'll try running it with CPython first because CPython doesn't have this
problem making `PyObject`s---that is just the default object representation in
the runtime.

```console
$ python3.10 setup.py build
$ time python3.10 bench.py
10000000
846.6ms
$
```

Okay so the text output is a little fudged since I actually measured this with
`hyperfine`, but you get the idea. CPython takes a very respectable 850ms to go
back and forth with C *10 million times*.

Now let's see how PyPy does on time, since it's doing a lot more work at the
boundary.

```console
$ pypy3.10 setup.py build
$ time pypy3.10 bench.py
10000000
2.269s
$
```

Yeah, okay, so all that extra unnecessary work that PyPy does (before our
changes) ends up really adding up. Our benchmark of `inc` takes *three times as
long* as CPython. Oof. But this post is all about adding types. What if we add
types to the C module and measure *with* our changes to PyPy?

Here are the changes to the C module:

```diff
diff --git a/tmp/cext.c b/tmp/cext-typed.c
index 8f5b31f..52678cb 100644
--- a/tmp/cext.c
+++ b/tmp/cext-typed.c
@@ -14,8 +14,15 @@ PyObject* inc(PyObject* module, PyObject* obj) {
   return PyLong_FromLong(result);
 }

+PyPyTypedMethodMetadata inc_sig = {
+  .arg_type = T_C_LONG,
+  .ret_type = T_C_LONG,
+  .underlying_func = inc_impl,
+  .ml_name = "inc",
+};
+
 static PyMethodDef mytypedmod_methods[] = {
-    {"inc", inc, METH_O, "Add one to an int"},
+    {inc_sig.ml_name, inc, METH_O | METH_TYPED, "Add one to an int"},
     {NULL, NULL, 0, NULL}};

 static struct PyModuleDef mytypedmod_definition = {
```

And now let's run it with our new patched PyPy:

```console
$ pypy3.10-patched setup.py build
$ time pypy3.10-patched bench.py
10000000
168.1ms
$
```

<!-- TODO(max): Compare with Skybison -->

168ms! To refresh your memory, that's **5x** faster than CPython and **13x**
faster than baseline PyPy. I honestly did not believe my eyes when I saw this
number. And CF and I think there is *still room for more*
improvements like doing the signature/metadata finding inside the JIT instead
of calling that C function.

This is extraordinarily promising.

But as I said before, most applications don't consist of a Python program
calling a C function and only a C function in a tight loop. It would be
important to profile how this change affects a *representative* workload. That
would help motivate the inclusion of these type signatures in a binding
generator such as Cython.

In the meantime, let's take a look at how the changes look in the PyPy
codebase.

## Implementing in PyPy: PyPy internals

<!-- TODO: flesh out -->

PyPy is comprised of two main parts:

* A Python interpreter
* A tool to transform interpreters into JIT compilers

This means that instead of writing fancy JIT compiler changes to get this to
work, we wrote an interpreter change. Their `cpyext` (C API) handling code
already contains a little "interpreter" of sorts to make calls to C extensions.
It looks at `ml_flags` to distinguish between `METH_O` and `METH_FASTCALL`, for
example.

So we added a new case that looks like this pseudocode:

```diff
diff --git a/tmp/interp.py b/tmp/typed-interp.py
index 900fa9c..b973f13 100644
--- a/tmp/interp.py
+++ b/tmp/typed-interp.py
@@ -1,7 +1,17 @@
 def make_c_call(meth, args):
     if meth.ml_flags & METH_O:
         assert len(args) == 1
+        if meth.ml_flags & METH_TYPED:
+            return handle_meth_typed(meth, args[0])
         return handle_meth_o(meth, args[0])
     if meth.ml_flags & METH_FASTCALL:
         return handle_meth_fastcall(meth, args)
     # ...
+
+def handle_meth_typed(meth, arg):
+    sig = call_scary_c_function_to_find_sig(meth)
+    if isinstance(arg, int) and sig.arg_type == int and sig.ret_type == int:
+        unboxed_arg = convert_to_unboxed(arg)
+        unboxed_result = call_f_func(sig.underlying_func, unboxed_arg)
+        return convert_to_boxed(unboxed_result)
+    # ...
```

Since the JIT probably already knows about the types of the arguments to the C
extension function (and probably has also unboxed them), all of the
intermediate checks and allocation can be elided. This makes for much less
work!

To check out the actual changes to PyPy, look at [this stack of
commits](https://github.com/pypy/pypy/compare/3a06bbe5755a1ee05879b29e1717dcbe230fdbf8...branches/py3.10-mb-typed-sig-experiments).

<!--
To see what this looks like from the JIT's perspective, we can peek at the
generated IR before and after the change.

TODO(max): Add PyPy traces before/after -->

## Next steps

This project isn't merged or finished. While we have a nice little test suite
and a microbenchmark, ideally we would do some more:

* Get other native types (other int types, double) working
* Get multiple parameters working (fastcall)
* Hack a proof of concept of this idea into Cython
* Make the signature more expressive
  * Perhaps we should have a mini language kind of like CPython's Argument
    Clinic
* Integrate this into other runtimes such as GraalPython or even CPython
  * While this won't help CPython *just* yet, they might find it useful when
    they do some more optimizations in the JIT

Let us know if you have any ideas!

<!-- TODO(max): Other languages? -->
<!-- TODO(max): Static Python and type declarations with unboxed types -->

## Updates and other ideas

*lifthrasiir* on Hacker News [points
out](https://news.ycombinator.com/item?id=38989823) that this struct should be
versioned for future-proofing. They also suggest potentially avoiding
`METH_TYPED` by shipping a sort of sibling symbol `_PyPyTyped_foo` for each
`foo`. That's interesting.

Wenzel Jakob (of pybind11 and nanobind fame) wrote in that we should not ignore
overloads (apparently fairly common in the wild) in our implementation.
Apparently people like to make, for example, `myextension.abs(x)` where the
call to `abs_float` or `abs_int` is dispatched in the binding glue (a real
example that I found in a search on GitHub is [available
here](https://github.com/lief-project/LIEF/blob/58f499a22221cee7ab6682986b1504b90acd8556/api/python/src/ELF/init.cpp#L100)).
A JIT like PyPy could make the dynamic dispatch go away.

Another idea, from CF, a little more difficult: what if the Cython
compiler could generate Python code or bytecode? Or, what if PyPy could ingest
Cython code?

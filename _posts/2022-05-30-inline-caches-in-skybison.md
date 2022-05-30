---
title: "Inline caches in the Skybison runtime"
layout: post
date: 2022-05-30
series: runtime-opt
---

Inline caching is a popular technique for optimizing dynamic language runtimes.
I have written about it before ([post 1](/blog/inline-caching/) and
[post 2](/blog/inline-caching-quickening/)), using an artificial sample
interpreter.

While this is good for illustrating the core technique, the simplified
interpreter core does not have any real-world requirements or complexity: its
object model models nothing of interest; the types are immutable; and there is
no way to program for this interpreter using a text programming language. The
result is somewhat unsatisfying.

In this post, I will write about the inline caching implementation in
[Skybison](https://github.com/tekknolagi/skybison), a relatively complete
Python runtime originally developed for use in Instagram. It nicely showcases
all of the fun and sharp edges of the Python object model and how we solved
hard problems.

In order to better illustrate the design choices we made when building
Skybison, I will often side-by-side it with CPython, the most popular
implementation of Python. This is not meant to degrade CPython; it is the
reference implementation, it is extremely widely used, and it is still being
actively developed. In fact, later (currently to-be-released) versions of
CPython use similar techniques to those shown here.

## Optimization decisions

This post will talk about the inline caching system and in the process mention
a host of performance features built in to Skybison. They work *really well
together* but ultimately they are orthogonal features and should not be
conflated. Some of these ideas are:

* Immediate objects
* Compact objects
* Layouts
* Inline caching and cache invalidation
* Quickening
* Assembly interpreter

## Loading attributes

The normal path for attributes in CPython involves some generic dictionary
lookups and function calls. We can take a look at the opcode handler for
`LOAD_ATTR` in CPython and some of the library functions it uses to see what I
mean:

```c
TARGET(LOAD_ATTR) {
    PyObject *name = GETITEM(names, oparg);
    PyObject *owner = TOP();
    PyObject *res = PyObject_GetAttr(owner, name);
    Py_DECREF(owner);
    SET_TOP(res);
    if (res == NULL)
        goto error;
    DISPATCH();
}
```

The opcode handler fetches the top of the stack (the "owner" in CPython and
"receiver" in Skybison and a lot of Smalltalk-inspired languages, but the
important thing is that it's the left hand side of the `.`), reads the
string object name from a tuple on the code object, and passes them to
`PyObject_GetAttr`.

`PyObject_GetAttr` does the required dispatch for an attribute lookup. It first
ensures the attribute is a string, inspects the type of the receiver `v`, and
calls the appropriate dictionary lookup function.

```c
PyObject *
PyObject_GetAttr(PyObject *v, PyObject *name)
{
    PyTypeObject *tp = Py_TYPE(v);

    if (!PyUnicode_Check(name)) {
        PyErr_Format(PyExc_TypeError,
                     "attribute name must be string, not '%.200s'",
                     name->ob_type->tp_name);
        return NULL;
    }
    if (tp->tp_getattro != NULL)
        return (*tp->tp_getattro)(v, name);
    if (tp->tp_getattr != NULL) {
        char *name_str = PyUnicode_AsUTF8(name);
        if (name_str == NULL)
            return NULL;
        return (*tp->tp_getattr)(v, name_str);
    }
    PyErr_Format(PyExc_AttributeError,
                 "'%.50s' object has no attribute '%U'",
                 tp->tp_name, name);
    return NULL;
}
```

This is a *lot* of work. `PyObject_GetAttr` is meant to be the entrypoint for
most (all?) attribute lookups in CPython, so it has to handle every case. In
the common case---attribute lookups in the interpreter with `LOAD_ATTR`---it's
doing too much. One small example of this is the `PyUnicode_Check`. We know in
the interpreter that the attribute name will always be a string! Why check
again?

Skybison also has a very generic `LOAD_ATTR` handler but it is only used if
a function's bytecode cannot be optimized. I have reproduced it here for
reference so that you can see how similar it is to CPython's:

```cpp
HANDLER_INLINE Continue Interpreter::doLoadAttr(Thread* thread, word arg) {
  Frame* frame = thread->currentFrame();
  HandleScope scope(thread);
  Object receiver(&scope, thread->stackTop());
  Tuple names(&scope, Code::cast(frame->code()).names());
  Str name(&scope, names.at(arg));
  RawObject result = thread->runtime()->attributeAt(thread, receiver, name);
  if (result.isErrorException()) return Continue::UNWIND;
  thread->stackSetTop(result);
  return Continue::NEXT;
}
```

The more common case in Skybison is a cached attribute lookup. If you have read
my mini-series on inline caching in the made-up interpreter, you will know that
I encoded a state machine in the opcode handlers for method lookup: the
uncached opcode handler transitioned to either a caching version once it saw
its first objects. Skybison does something similar, but it does it better.

Skybison starts with `LOAD_ATTR_ANAMORPHIC`: the state that has seen no
objects. When it is first executed, it does a full attribute
lookup---dictionaries and all---and then caches the result if possible.

```cpp
HANDLER_INLINE Continue Interpreter::doLoadAttrAnamorphic(Thread* thread,
                                                          word arg) {
  word cache = currentCacheIndex(thread->currentFrame());
  return loadAttrUpdateCache(thread, arg, cache);
}
```

Depending on the type of the receiver and the type of the attribute value that was
found on the object (yes, this is important!), we rewrite the current opcode to
something more specialized. I have annotated `loadAttrUpdateCache`:

```cpp
Continue Interpreter::loadAttrUpdateCache(Thread* thread, word arg,
                                          word cache) {
  HandleScope scope(thread);
  Frame* frame = thread->currentFrame();
  Function function(&scope, frame->function());
  // ...
  Object receiver(&scope, thread->stackTop());
  Str name(&scope, Tuple::cast(Code::cast(frame->code()).names()).at(arg));

  Object location(&scope, NoneType::object());
  LoadAttrKind kind;
  // Do the full attribute lookup. Attribute kind, if applicable, is stored in
  // `kind` and and location information, if applicable, is stored in
  // `location`.
  Object result(&scope, thread->runtime()->attributeAtSetLocation(
                            thread, receiver, name, &kind, &location));
  // The attribute might not exist or execution of the attribute lookup might
  // have raised an exception; attribute lookups can execute arbitrary code.
  if (result.isErrorException()) return Continue::UNWIND;
  if (location.isNoneType()) {
    // We can't cache this looup for some reason.
    thread->stackSetTop(*result);
    return Continue::NEXT;
  }

  // Cache the attribute load.
  MutableTuple caches(&scope, frame->caches());
  ICState ic_state = icCurrentState(*caches, cache);
  Function dependent(&scope, frame->function());
  LayoutId receiver_layout_id = receiver.layoutId();
  if (ic_state == ICState::kAnamorphic) {
    switch (kind) {
      case LoadAttrKind::kInstanceOffset:
        rewriteCurrentBytecode(frame, LOAD_ATTR_INSTANCE);
        icUpdateAttr(thread, caches, cache, receiver_layout_id, location, name,
                     dependent);
        break;
      case LoadAttrKind::kInstanceFunction:
        rewriteCurrentBytecode(frame, LOAD_ATTR_INSTANCE_TYPE_BOUND_METHOD);
        icUpdateAttr(thread, caches, cache, receiver_layout_id, location, name,
                     dependent);
        break;
      case LoadAttrKind::kInstanceProperty:
        rewriteCurrentBytecode(frame, LOAD_ATTR_INSTANCE_PROPERTY);
        icUpdateAttr(thread, caches, cache, receiver_layout_id, location, name,
                     dependent);
        break;
      case LoadAttrKind::kInstanceSlotDescr:
        rewriteCurrentBytecode(frame, LOAD_ATTR_INSTANCE_SLOT_DESCR);
        icUpdateAttr(thread, caches, cache, receiver_layout_id, location, name,
                     dependent);
        break;
      case LoadAttrKind::kInstanceType:
        rewriteCurrentBytecode(frame, LOAD_ATTR_INSTANCE_TYPE);
        icUpdateAttr(thread, caches, cache, receiver_layout_id, location, name,
                     dependent);
        break;
      case LoadAttrKind::kInstanceTypeDescr:
        rewriteCurrentBytecode(frame, LOAD_ATTR_INSTANCE_TYPE_DESCR);
        icUpdateAttr(thread, caches, cache, receiver_layout_id, location, name,
                     dependent);
        break;
      case LoadAttrKind::kModule: {
        ValueCell value_cell(&scope, *location);
        DCHECK(location.isValueCell(), "location must be ValueCell");
        icUpdateAttrModule(thread, caches, cache, receiver, value_cell,
                           dependent);
      } break;
      case LoadAttrKind::kType:
        icUpdateAttrType(thread, caches, cache, receiver, name, location,
                         dependent);
        break;
      default:
        UNREACHABLE("kinds should have been handled before");
    }
  } else {
    // ...
  }
  thread->stackSetTop(*result);
  return Continue::NEXT;
}
```

Then we never see the opcode handler for `LOAD_ATTR_ANAMORPHIC` again! The
opcode has monomorphic specialization: it has been specialized for the one type
it has seen.

We have also stored the receiver's layout ID and attribute offset in the cache
with `icUpdateAttr`. `icUpdateAttr` handles writing to the cache, including
both the write to the cache tuple and registering dependencies---which we'll
talk about later.

For now, we'll take a tour through the monomorphic attribute handler.

### Monomorphic

There is a lot going on here but a very common case is a field on an object,
like the below Python code, so we will focus on that:

```python
class C:
  def __init__(self):
    self.value = 5

def lookup_value(obj):
  return obj.value

# First: anamorphic -> monomorphic
lookup_value(C())
# Second: monomorphic!
lookup_value(C())
```

This case is the `LoadAttrKind::kInstanceOffset` case. We have a special opcode
to handle this: `LOAD_ATTR_INSTANCE`.

```cpp
HANDLER_INLINE Continue Interpreter::doLoadAttrInstance(Thread* thread,
                                                        word arg) {
  Frame* frame = thread->currentFrame();
  word cache = currentCacheIndex(frame);
  RawMutableTuple caches = MutableTuple::cast(frame->caches());
  RawObject receiver = thread->stackTop();
  bool is_found;
  RawObject cached =
      icLookupMonomorphic(caches, cache, receiver.layoutId(), &is_found);
  if (!is_found) {
    EVENT_CACHE(LOAD_ATTR_INSTANCE);
    return Interpreter::loadAttrUpdateCache(thread, arg, cache);
  }
  RawObject result = loadAttrWithLocation(thread, receiver, cached);
  thread->stackSetTop(result);
  return Continue::NEXT;
}
```

This opcode handler does much less than the generic version. It does a
monomorphic lookup (a load and compare) and then a load from the receiver. If
the types don't match then we do another full lookup and transition to a
polymorphic (multiple types) opcode, which we'll look at in the next section.

```cpp
inline RawObject icLookupMonomorphic(RawMutableTuple caches, word cache,
                                     LayoutId layout_id, bool* is_found) {
  word index = cache * kIcPointersPerEntry;
  DCHECK(!caches.at(index + kIcEntryKeyOffset).isUnbound(),
         "cache.at(index) is expected to be monomorphic");
  RawSmallInt key = SmallInt::fromWord(static_cast<word>(layout_id));
  if (caches.at(index + kIcEntryKeyOffset) == key) {
    *is_found = true;
    return caches.at(index + kIcEntryValueOffset);
  }
  *is_found = false;
  return Error::notFound();
}
```

This looks like a lot of code still but it actually only takes a few machine
instructions in the fast path. When optimized by a C++ compiler, this function
does:

* A memory load from the frame to get the cache index
* A memory load from the frame to get the caches tuple
* A memory load from the stack to get the receiver
* A memory load from the receiver (if it's a heap object) to get its layout ID
* A memory load from the caches tuple to get the stored layout ID
* A comparison with the receiver layout ID to see if they are the same
* A memory load from the caches tuple to get the cached attribute offset
* A memory load from the receiver to get the cached attribute
* A memory store to the stack to push the result

This could probably still be reduced further by hand, which we have done in our
[assembly implementation][asm-load-attr-instance] of the Python interpreter.

[asm-load-attr-instance]: https://github.com/tekknolagi/skybison/blob/9253d1e0e42c756dfa37e709918266e09e1d15dc/runtime/interpreter-gen-x64.cpp#L1130

But what about the slow path? What if we are seeing a different type this time
around? Well, it's morphin' time. Let's transition to the polymorphic cache in
`loadAttrUpdateCache`:

```cpp
Continue Interpreter::loadAttrUpdateCache(Thread* thread, word arg,
                                          word cache) {
  // ... full attribute lookup here ...

  // Cache the attribute load
  MutableTuple caches(&scope, frame->caches());
  ICState ic_state = icCurrentState(*caches, cache);
  Function dependent(&scope, frame->function());
  LayoutId receiver_layout_id = receiver.layoutId();
  if (ic_state == ICState::kAnamorphic) {
    // ...
  } else {
    DCHECK(
        currentBytecode(thread) == LOAD_ATTR_INSTANCE ||
            currentBytecode(thread) == LOAD_ATTR_INSTANCE_TYPE_BOUND_METHOD ||
            currentBytecode(thread) == LOAD_ATTR_POLYMORPHIC,
        "unexpected opcode");
    switch (kind) {
      case LoadAttrKind::kInstanceOffset:
      case LoadAttrKind::kInstanceFunction:
        rewriteCurrentBytecode(frame, LOAD_ATTR_POLYMORPHIC);
        icUpdateAttr(thread, caches, cache, receiver_layout_id, location, name,
                     dependent);
        break;
      default:
        break;
    }
  }
  thread->stackSetTop(*result);
  return Continue::NEXT;
}
```

We transition from `LOAD_ATTR_INSTANCE` to `LOAD_ATTR_POLYMORPHIC` and also
(with `icUpdateAttr`) transition the cache from a monomorphic cache (key+value)
to a polymorphic cache (tuple of multiple key+value).

### Polymorphic

The polymorphic attribute lookup is shared by

```cpp
HANDLER_INLINE Continue Interpreter::doLoadAttrPolymorphic(Thread* thread,
                                                           word arg) {
  Frame* frame = thread->currentFrame();
  RawObject receiver = thread->stackTop();
  LayoutId layout_id = receiver.layoutId();
  word cache = currentCacheIndex(frame);
  bool is_found;
  RawObject cached = icLookupPolymorphic(MutableTuple::cast(frame->caches()),
                                         cache, layout_id, &is_found);
  if (!is_found) {
    EVENT_CACHE(LOAD_ATTR_POLYMORPHIC);
    return loadAttrUpdateCache(thread, arg, cache);
  }
  RawObject result = loadAttrWithLocation(thread, receiver, cached);
  thread->stackSetTop(result);
  return Continue::NEXT;
}
```

## Loading methods

### Monomorphic

### Polymorphic

## Modifying types

## Thanks

Thank you to all of the many people who wrote great language runtimes, people
who preceded me on the team, and people who I worked with on the team. You came
up with most of the ideas. I just chronicle them here.

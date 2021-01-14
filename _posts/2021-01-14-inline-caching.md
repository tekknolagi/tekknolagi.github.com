---
title: Inline caching
layout: post
date: 2021-01-14 00:00:00 PT
---

Inline caching is a popular technique for runtime optimization. It was first
introduced in 1984 in Deutsch &amp; Schiffman's paper [Efficient implementation
of the smalltalk-80 system [PDF]][smalltalk] but has had a long-lasting legacy in
today's dynamic language implementations. Runtimes like the Hotspot JVM, V8,
and SpiderMonkey use it to improve the performance of code written for those
virtual machines.

[smalltalk]: http://web.cs.ucla.edu/~palsberg/course/cs232/papers/DeutschSchiffman-popl84.pdf

In this blog post, I will attempt to distill the essence of inline caching
using a small and relatively useless bytecode interpreter built solely for this
blog post. The caching strategy in this demo is a technique similar to the
ideas from [Inline Caching meets Quickening [PDF]][ic-quickening] in that it
caches function pointers instead of making use of a JIT compiler.

[ic-quickening]: http://www.complang.tuwien.ac.at/kps09/pdfs/brunthaler.pdf

## Background

In many compiled programming languages like C and C++, types and attribute
locations are known at compile time. This makes code like the following fast:

```c
#include "foo.h"

Foo do_add(Foo left, Foo right) {
  return left.add(right);
}
```

The compiler knows precisely what type `left` and `right` are (it's `Foo`) and
also where the method `add` is in the executable. If the implementation is in
the header file, it may even be inlined and `do_add` may be optimized to a
single instruction. Check out the assembly from `objdump`:

```
0000000000401160 <_Z6do_add3FooS_>:
  401160:	48 83 ec 18          	sub    $0x18,%rsp
  401164:	89 7c 24 0c          	mov    %edi,0xc(%rsp)
  401168:	48 8d 7c 24 0c       	lea    0xc(%rsp),%rdi
  40116d:	e8 0e 00 00 00       	callq  401180 <_ZN3Foo3addES_>
  401172:	48 83 c4 18          	add    $0x18,%rsp
  401176:	c3                   	retq   
```

All it does is save the parameters to the stack, call `Foo::add`, and then
restore the stack.

In more dynamic programming languages, it is often impossible to determine at
runtime startup what type any given variable binding has. We'll use Python as an
example to illustrate how dynamism makes this tricky, but this constraint is
broadly applicable to Ruby, JavaScript, etc.

Consider the following Python snippet:

```python
def do_add(left, right):
    return left.add(right)
```

Due to Python's various dynamic features, the compiler cannot in general know
what type `value` is and therefore what code to run when reading `left.add`.
This program will be compiled down to a couple Python bytecode instructions
that do a very generic `LOAD_METHOD`/`CALL_METHOD` operation:

```
>>> import dis
>>> dis.dis("""
... def do_add(left, right):
...     return left.add(right)
... """)
[snip]
Disassembly of <code object do_add at 0x7f0b40cf49d0, file "<dis>", line 2>:
  3           0 LOAD_FAST                0 (left)
              2 LOAD_METHOD              0 (add)
              4 LOAD_FAST                1 (right)
              6 CALL_METHOD              1
              8 RETURN_VALUE

>>> 
```

This `LOAD_METHOD` Python bytecode instruction is unlike the x86 `mov`
instruction in that `LOAD_METHOD` is not given an offset into `left`, but
instead is given the name `"add"`. It has to go and figure out how to read
`add` from `left`'s type --- which could change from call to call.

In fact, even if the parameters were typed (which is a new feature in Python
3), the same code would be generated. Writing `left: Foo` means that `left` is a
`Foo` *or* a subclass.

This is not a simple process like "fetch the attribute at the given offset
specified by the type". The runtime has to find out what kind of object `add`
is. Maybe it's just a function, or maybe it's a `property`, or maybe it's some
custom descriptor protocol thing. There's no way to just turn this into a
`mov`!

... or is there?

## Runtime type information

Though dynamic runtimes do not know ahead of time what types variables have at
any given opcode, they do eventually find out *when the code is run*. The first
time someone calls `do_add`, `LOAD_METHOD` will go and look up the type of
`left`. It will use it to look up the attribute `add` and then throw the type
information away. But the second time someone calls `do_add`, the same thing
will happen. Why don't runtimes store this information about the type and the
method and save the lookup work?

The thinking is "well, `left` could be any type of object --- best not make any
assumptions about it." While this is *technically* true, Deutsch &amp;
Schiffman find that "at a given point in code, the receiver is often the same
class as the receiver at the same point when the code was last executed".

> **Note:** By *receiver*, they mean the thing from which the attribute is
> being loaded. This is some Object-Oriented Programming terminology.

This is huge. This means that, even in this sea of dynamic behavior, humans
actually are not all that creative and tend to write functions that see only a
handful of types at a given location.

The Smalltalk-80 paper describes a runtime that takes advantage of this by
adding "inline caches" to functions. These inline caches keep track of variable
types seen at each point in the code, so that the runtime can make optimization
decisions with that information.

Let's take a look at how this could work in practice.

## A small example

I put together a [small stack machine][repo] with only a few operations. There
are very minimal features to avoid distracting from the main focus: inline
caching. Extending this example would be an excellent exercise.

[repo]: https://github.com/tekknolagi/icdemo

### Objects and types

The design of this runtime involves two types of objects (`int`s and `str`s).
Objects are implemented as a tagged union, but for the purposes of this blog
post the representation does not matter very much.

```c
typedef enum {
  kInt,
  kStr,
} ObjectType;

typedef struct {
  ObjectType type;
  union {
    const char *str_value;
    int int_value;
  };
} Object;
```

These types have methods on them, such as `add` and `print`. Method names are
represented with an enum (`Symbol`) though strings would work just as well.

```c
typedef enum {
  kAdd,
  kPrint,

  kUnknownSymbol = kPrint + 1,
} Symbol;
```

The representation of type information isn't super important. Just know that
there is a function called `lookup_method` and that it is very slow. Eventually
we'll want to cache its result.

```c
Method lookup_method(ObjectType type, Symbol name);
```

Let's see how we use these `lookup_method` in the interpreter.

### Interpreter

There's no way to call these methods directly. For the purposes of this demo,
the only way to call these methods is through purpose-built opcodes. For
example, the opcode `ADD` takes two arguments. It looks up `kAdd` on the left
hand side and calls it. `PRINT` is similar.

There are only two other opcodes, `ARG` and `HALT`.

```c
typedef enum {
  // Load a value from the arguments array at index `arg'.
  ARG,
  // Add stack[-2] + stack[-1].
  ADD,
  // Pop the top of the stack and print it.
  PRINT,
  // Halt the machine.
  HALT,
} Opcode;
```
Bytecode is represented by a series of opcode/argument pairs, each taking up
one byte. Only `ARG` needs an argument; the other instructions ignore theirs.

Let's look at a sample program.

```c
byte bytecode[] = {/*0:*/ ARG,   0,
                   /*2:*/ ARG,   1,
                   /*4:*/ ADD,   0,
                   /*6:*/ PRINT, 0,
                   /*8:*/ HALT,  0};
```

This program takes its two arguments, adds them together, prints the result,
and then halts the interpreter.

You may wonder, "how is it that there is an instruction for loading arguments
but no call instruction?" Well, the interpreter does not support calls. There
is only a top-level function, `eval_code`. It takes an object, evaluates its
bytecode with the given arguments, and returns. Extending the interpreter to
support function calls would be another good exercise.

The interpreter implementation is a fairly straightforward `switch` statement.
Notice that it takes a representation of a function-like thing (`Code`) and an
array of arguments. `nargs` is only used for bounds checking.

```c
typedef unsigned char byte;

typedef struct {
  ObjectType key;
  Method value;
} CachedValue;

typedef struct {
  // Array of `num_opcodes' (op, arg) pairs (total size `num_opcodes' * 2).
  byte *bytecode;
  int num_opcodes;
  // Array of `num_opcodes' elements.
  CachedValue *caches;
} Code;

static unsigned kBytecodeSize = 2;

void eval_code_uncached(Code *code, Object *args, int nargs) {
  int pc = 0;
#define STACK_SIZE 100
  Object stack_array[STACK_SIZE];
  Object *stack = stack_array;
#define PUSH(x) *stack++ = (x)
#define POP() *--stack
  while (true) {
    Opcode op = code->bytecode[pc];
    byte arg = code->bytecode[pc + 1];
    switch (op) {
      case ARG:
        CHECK(arg < nargs && "out of bounds arg");
        PUSH(args[arg]);
        break;
      case ADD: {
        Object right = POP();
        Object left = POP();
        Method method = lookup_method(left.type, kAdd);
        Object result = (*method)(left, right);
        PUSH(result);
        break;
      }
      case PRINT: {
        Object obj = POP();
        Method method = lookup_method(obj.type, kPrint);
        (*method)(obj);
        break;
      }
      case HALT:
        return;
      default:
        fprintf(stderr, "unknown opcode %d\n", op);
        abort();
    }
    pc += kBytecodeSize;
  }
}
```

Both `ADD` and `PRINT` make use of `lookup_method` to find out what function
pointer corresponds to the given `(type, symbol)` pair. Both opcodes throw away
the result. How sad. Let's figure out how to save some of that data. Maybe we
can use the `caches` slot in `Code`.

### Inline caching strategy

Since the Smalltalk-80 paper tells us that the receiver type is unlikely to
change from call to call at a given point in the bytecode, let's cache *one*
method address per opcode. As with any cache, we'll have to store both a key
(the object type) and a value (the method address).

There are several states that the cache could be in when entering the an
opcode:

1. **If it is empty**, look up the method and store it in the cache using the
   current type as a cache key. Use the cached value.
1. **If it has an entry and the entry is for the current type**, use the cached
   value.
1. Last, **if it has an entry and the entry is for a different type**,
   invalidate the cache. Repeat the same steps as in the empty case.

This is a simple *monomorphic* (one element) implementation that should give us
most of the performance. A good exercise would be to extend this cache system
to be *polymorphic* (multiple elements) if the interpreter sees many types. For
that you will want to check out [Optimizing Dynamically-Typed Object-Oriented
Languages With Polymorphic Inline Caches][pic] by Hölzle, Chambers, and Ungar.

[pic]: http://brenocon.com/self%20-%20polymorphic%20inline%20caching%20-%20ecoop91.pdf

For the purposes of this inline caching demo, we will focus on caching lookups
in `ADD`. This is a fairly arbitrary choice in our simple runtime, since the
caching implementation will not differ between opcodes.

### Inline caching implementation

Let's think back to this `CachedValue *caches` array.

```c
typedef struct {
  ObjectType key;
  Method value;
} CachedValue;
```

This looks like it'll suit us just fine. Each element has a key and a value.
Each `Code` object has an array of these, one per opcode.

Let's see what changed in `ADD`.

```c
void eval_code_cached(Code *code, Object *args, int nargs) {
  // ...
#define CACHE_AT(pc) code->caches[(pc) / kBytecodeSize]
  while (true) {
    // ...
    switch (op) {
      // ...
      case ADD: {
        Object right = POP();
        Object left = POP();
        CachedValue cached = CACHE_AT(pc);
        Method method = cached.value;
        if (method == NULL || cached.key != left.type) {
          // Case 1 and 3
          method = lookup_method(left.type, kAdd);
          CACHE_AT(pc) = (CachedValue){.key = left.type, .value = method};
        }
        Object result = (*method)(left, right);
        PUSH(result);
        break;
      }
      // ...
    }
    pc += kBytecodeSize;
  }
}
```

Now instead of always calling `lookup_method`, we do two quick checks first. If
we have a cached value and it matches, we use that instead. So not much,
really, except for the reading and writing to `code->caches`.

### Run a test program and see results

Let's put it all together for some satisfying results. We can use the sample
program from earlier that adds its two arguments.

We'll call it four times. The first time we will call it with integer
arguments, and it will cache the integer method. The second time, it will use
the cached integer method. The third time, we will call it with string
arguments and it will cache the string method. The fourth time, it will use the
cached string method.

```c
int main() {
  byte bytecode[] = {/*0:*/ ARG,   0,
                     /*2:*/ ARG,   1,
                     /*4:*/ ADD,   0,
                     /*6:*/ PRINT, 0,
                     /*8:*/ HALT,  0};
  Object int_args[] = {
      new_int(5),
      new_int(10),
  };
  Object str_args[] = {
      new_str("hello "),
      new_str("world"),
  };
  Code code = new_code(bytecode, sizeof bytecode / kBytecodeSize);
  void (*eval)() = eval_code_cached;
  eval(&code, int_args, ARRAYSIZE(int_args));
  eval(&code, int_args, ARRAYSIZE(int_args));
  eval(&code, str_args, ARRAYSIZE(str_args));
  eval(&code, str_args, ARRAYSIZE(str_args));
}
```

And if we run that, we see:

```
laurel% ./a.out
int: 15
int: 15
str: "hello world"
str: "hello world"
```

Which superficially seems like it's working, at least. `5 + 10 == 15` and
`"hello " + "world" == "hello world"` after all.

To get an insight into the behavior of the caching system, I added some logging
statements. This will help convince us that the cache code does the right
thing.

```
laurel% ./a.out
updating cache at 4
int: 15
using cached value at 4
int: 15
updating cache at 4
str: "hello world"
using cached value at 4
str: "hello world"
```

Hey-ho, looks like it worked.

## Conclusion

Inline caches can be a good way to speed up your bytecode interpreter. I hope
this post helped you understand inline caches. Please write me if it did not.

## Exploring further

There are a number of improvements that could be made to this very simple demo.
I will list some of them below:

* Rewrite generic opcodes like `ADD` to type-specialized opcodes like
  `ADD_INT`. These opcodes will still have to check the types passed in, but
  can make use of a direct call instruction or even inline the specialized
  implementation. This technique is mentioned in [Efficient Interpretation
  using Quickening [PDF]][quickening] and is used in by the JVM.
* Not all opcodes require caches, yet we allocate for them anyway. How can we
  eliminate these wasted cache slots?
* Actually store caches inline in the bytecode, instead of off in a side table.
* Instead of storing cached function pointers, build up a sort of "linked list"
  of assembly stubs that handle different type cases. This is also mentioned in
  the Deutsch &amp; Schiffman paper. See for example [this
  excellent post][assembly] by Matthew Gaudet that explains it with pseudocode.
* Figure out what happens if the runtime allows for mutable types. How will you
  invalidate all the relevant caches?

[quickening]: https://publications.sba-research.org/publications/dls10.pdf
[assembly]: https://www.mgaudet.ca/technical/2018/6/5/an-inline-cache-isnt-just-a-cache

Maybe I will even write about them in the future.

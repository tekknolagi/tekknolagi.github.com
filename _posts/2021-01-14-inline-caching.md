---
title: Inline caching
layout: post
date: 2021-01-14 00:00:00 PT
description: Optimizing bytecode interpreters by avoiding method lookups
series: runtime-opt
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

In order to make the most of this post, I recommend having some background on
building bytecode virtual machines. It is by no means necessary, but will make
some of the *new* stuff easier to absorb.

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
what type `left` is and therefore what code to run when reading `left.add`.
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

This is not a simple process like "call the function at the given address
specified by the type". The runtime has to find out what kind of object `add`
is. Maybe it's just a function, or maybe it's a `property`, or maybe it's some
custom descriptor protocol thing. There's no way to just turn this into a
`call` instruction!

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

  kUnknownSymbol,
} Symbol;
```

The representation of type information isn't super important. Just know that
there is a function called `lookup_method` and that it is very slow. Eventually
we'll want to cache its result.

```c
Method lookup_method(ObjectType type, Symbol name);
```

Let's see how we use `lookup_method` in the interpreter.

### Interpreter

This interpreter provides no way to look up (`LOAD_METHOD`) or call
(`CALL_METHOD`) the methods directly. For the purposes of this demo, the only
way to call these methods is through purpose-built opcodes. For example, the
opcode `ADD` takes two arguments. It looks up `kAdd` on the left hand side and
calls it. `PRINT` is similar.

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
is only a top-level function, `eval_code_uncached`. It takes an object,
evaluates its bytecode with the given arguments, and returns. Extending the
interpreter to support function calls would be another good exercise.

The interpreter implementation is a fairly straightforward `switch` statement.
Notice that it takes a representation of `Frame`, which holds all the state,
like the `pc` and the `stack`. It contains a function-like thing (`Code`) and
an array of arguments. `nargs` is only used for bounds checking.

I am omitting some of its helper functions (`init_frame`, `push`, `pop`,
`peek`) for brevity's sake, but they do nothing tricky. Feel free to look in
the [repo][repo] for their definitions.

```c
typedef unsigned char byte;

#define STACK_SIZE 100

typedef struct {
  Object* stack_array[STACK_SIZE];
  Object** stack;
  Code* code;
  word pc;
  Object** args;
  word nargs;
} Frame;

void eval_code_uncached(Frame* frame) {
  Code* code = frame->code;
  while (true) {
    Opcode op = code->bytecode[frame->pc];
    byte arg = code->bytecode[frame->pc + 1];
    switch (op) {
      case ARG:
        CHECK(arg < frame->nargs && "out of bounds arg");
        push(frame, frame->args[arg]);
        break;
      case ADD: {
        Object* right = pop(frame);
        Object* left = pop(frame);
        Method method = lookup_method(object_type(left), kAdd);
        Object* result = (*method)(left, right);
        push(frame, result);
        break;
      }
      case PRINT: {
        Object* obj = pop(frame);
        Method method = lookup_method(object_type(obj), kPrint);
        (*method)(obj);
        break;
      }
      case HALT:
        return;
      default:
        fprintf(stderr, "unknown opcode %d\n", op);
        abort();
    }
    frame->pc += kBytecodeSize;
  }
}
```

Both `ADD` and `PRINT` make use of `lookup_method` to find out what function
pointer corresponds to the given `(type, symbol)` pair. Both opcodes throw away
the result. How sad. Let's figure out how to save some of that data.

### Inline caching strategy

Since the Smalltalk-80 paper tells us that the receiver type is unlikely to
change from call to call at a given point in the bytecode, let's cache *one*
method address per opcode. As with any cache, we'll have to store both a key
(the object type) and a value (the method address).

There are several states that the cache could be in when entering an opcode:

1. **If it is empty**, look up the method and store it in the cache using the
   current type as a cache key. Use the cached value.
1. **If it has an entry and the entry is for the current type**, use the cached
   value.
1. Last, **if it has an entry and the entry is for a different type**,
   flush the cache. Repeat the same steps as in the empty case.

This is a simple *monomorphic* (one element) implementation that should give us
most of the performance. A good exercise would be to extend this cache system
to be *polymorphic* (multiple elements) if the interpreter sees many types. For
that you will want to check out [Optimizing Dynamically-Typed Object-Oriented
Languages With Polymorphic Inline Caches][pic] by Hölzle, Chambers, and Ungar.

[pic]: http://brenocon.com/self%20-%20polymorphic%20inline%20caching%20-%20ecoop91.pdf

For the purposes of this inline caching demo, we will focus on caching lookups
in `ADD`. This is a fairly arbitrary choice in our simple runtime, since the
caching implementation will not differ between opcodes.

**Note:** The types in this demo code are *immutable*. Some programming
languages (Python, Ruby, etc) allow for types to be changed after they are
created. This requires careful cache invalidation. We will not address cache
invalidation in this post.

### Inline caching implementation

Let's store the caches on the `Code` struct. If we have one element per opcode,
that looks like:

```c
typedef struct {
  // Array of `num_opcodes' (op, arg) pairs (total size `num_opcodes' * 2).
  byte* bytecode;
  word num_opcodes;
  // Array of `num_opcodes' elements.
  CachedValue* caches;
} Code;
```

where `CachedValue` is a key/value pair of object type and method address:

```c
typedef struct {
  ObjectType key;
  Method value;
} CachedValue;
```

We have some helpers, `cache_at` and `cache_at_put`, to manipulate the caches.

```c
CachedValue cache_at(Frame* frame) {
  return frame->code->caches[frame->pc / kBytecodeSize];
}

void cache_at_put(Frame* frame, ObjectType key, Method value) {
  frame->code->caches[frame->pc / kBytecodeSize] =
      (CachedValue){.key = key, .value = value};
}
```

These functions are fairly straightforward given our assumption of a cache
present for every opcode.

Let's see what changed in the `ADD` opcode.

```c
void add_update_cache(Frame* frame, Object* left, Object* right) {
  Method method = lookup_method(object_type(left), kAdd);
  cache_at_put(frame, object_type(left), method);
  Object* result = (*method)(left, right);
  push(frame, result);
}

void eval_code_cached(Frame* frame) {
  // ...
  while (true) {
    // ...
    switch (op) {
      // ...
      case ADD: {
        Object* right = pop(frame);
        Object* left = pop(frame);
        CachedValue cached = cache_at(frame);
        Method method = cached.value;
        if (method == NULL || cached.key != object_type(left)) {
          add_update_cache(frame, left, right);
          break;
        }
        Object* result = (*method)(left, right);
        push(frame, result);
        break;
      }
      // ...
    }
    frame->pc += kBytecodeSize;
  }
}
```

Now instead of always calling `lookup_method`, we do two quick checks first. If
we have a cached value and it matches, we use that instead. So not much
changed, really, except for the reading and writing to `code->caches`.

If we don't have a cached value, we call `lookup_method` and store the result
in the cache. Then we call it. I pulled this slow-path code into the function
`add_update_cache`.

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
  Object* int_args[] = {
      new_int(5),
      new_int(10),
  };
  void (*eval)(Frame*) = eval_code_cached;
  Frame frame;
  Code code = new_code(bytecode, sizeof bytecode / kBytecodeSize);
  init_frame(&frame, &code, int_args, ARRAYSIZE(int_args));
  eval(&frame);
  init_frame(&frame, &code, int_args, ARRAYSIZE(int_args));
  eval(&frame);
  Object* str_args[] = {
      new_str("hello "),
      new_str("world"),
  };
  init_frame(&frame, &code, str_args, ARRAYSIZE(str_args));
  eval(&frame);
  init_frame(&frame, &code, str_args, ARRAYSIZE(str_args));
  eval(&frame);
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

## Performance analysis

Most posts like this end with some kind of performance analysis of the two
strategies proposed. Perhaps the author will do some kind of rigorous
statistical analysis of the performance of the code before &amp; after. Perhaps
the author will run the sample code in a loop 1,000 times and use `time` to
measure. Most authors do *something*.

Reader, I will not be doing a performance analysis in this post. This tiny
interpreter has little to no resemblance to real-world runtimes and this tiny
program has little to no resemblance to real-world workloads. I will, however,
give you some food for thought:

**If you were building a runtime, how would you know inline caching would help
you?**
Profile your code. Use the tools available to you, like Callgrind and Perf. If
you see your runtime's equivalent of `lookup_method` show up in the profiles,
consider that you may want a caching strategy. `lookup_method` may not show up
in all of your profiles. Some benchmarks will have very different workloads
than other benchmarks.

**How would you measure the impact of inline caching, once added?**
Profile your code. Did the percent CPU time of `lookup_method` decrease? What
about overall runtime? It's entirely possible that your benchmark *slowed
down*. This could be an indicator of polymorphic call sites --- which would
lead to a lot of overhead from cache eviction. In that case, you may want to
add a polymorphic inline cache. `printf`-style logging can help a lot in
understanding the characteristics of your benchmarks.

No matter how you measure impact, it is *not enough* to run the `ADD` handler
in a tight loop and call it a benchmark. The real-life workload for your
runtime will undoubtedly look very different. What percent of opcodes executed
is `ADD`? Ten percent? Five percent? You will be limited by [Amdahl's
Law][amdahl]. That will be your upper bound on performance
improvements.[^amdahl-kind-of]

[amdahl]: https://en.wikipedia.org/wiki/Amdahl%27s_law

[^amdahl-kind-of]: Kind of. Sometimes there are hardware effects that make your
    change that would otherwise be at most 5% faster actually significantly
    faster. For example, if you are thrashing your data cache by doing these
    method lookups and your inline cache implementation dramatically improves
    that, the neighboring code might also get faster because its data could be
    in cache, too. But you *really* need to measure these things before you
    make grand claims. See also the note in the next section of the post about
    things like memory pressure and swapping.

**What other performance considerations might you have?**
Consider your memory constraints. Perhaps you are on a system where memory is a
concern. Adding inline caches will require additional memory. This might cause
swapping, if its enabled.

**So without benchmarks, how do you know this is even faster?**
Don't take my word for it. Benchmark your runtime. Take a look at the
Smalltalk-80 and Brunthaler papers linked. Take a look at the JVM, V8, MRI,
Dart, and other runtimes. They all seem to have found inline caching helpful.

## Conclusion

Inline caches can be a good way to speed up your bytecode interpreter. I hope
this post helped you understand inline caches. Please write me if it did not.

## Exploring further

There are a number of improvements that could be made to this very simple demo.
I will list some of them below:

* Rewrite generic opcodes like `ADD` to type-specialized opcodes like
  `ADD_INT`. These opcodes will still have to check the types passed in, but
  can make use of a direct call instruction or even inline the specialized
  implementation. I wrote [a follow-up post](/blog/inline-caching-quickening/)
  about it!
* Not all opcodes require caches, yet we allocate for them anyway. How can we
  eliminate these wasted cache slots?
* Actually store caches inline in the bytecode, instead of off in a side table.
* Instead of storing cached function pointers, build up a sort of "linked list"
  of assembly stubs that handle different type cases. This is also mentioned in
  the Deutsch &amp; Schiffman paper. See for example [this
  excellent post][assembly] by Matthew Gaudet that explains it with pseudocode.
* Figure out what happens if the runtime allows for mutable types. How will you
  invalidate all the relevant caches?

[assembly]: https://www.mgaudet.ca/technical/2018/6/5/an-inline-cache-isnt-just-a-cache

Maybe I will even write about them in the future.

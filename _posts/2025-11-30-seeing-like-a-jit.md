---
title: "Seeing like a JIT"
layout: post
---

I work on optimizing dynamic language runtimes at scale.

A lot of people ask me about my job. Or at least, a lot of people make
statements to me that indicate that they fundamentally misunderstand my job.
Well, I don't know about a lot---it's happened a couple of times, anyway.

But a large part of my job[^real-job] is not fundamentally that complex. It's
complicated, sure---many moving parts, fiddly interlocking pieces of code,
coordinating people across timezones, languages, and countries---but the goal
is to make Ruby (or insert your choice of dynamic language here) faster, and
economically.

The first step is understanding this is learning about *the fast paths* and
*the tradeoffs*. That's what we'll talk about in this post.

[^real-job]: While the platonic ideal of my job might look like graph transforms,
    reading through how the current Intel microarchitecture works, bug fixing, and
    copying HotSpot's (or insert your choice of dynamic language platform here)
    engineering decisions, it's mostly *not* that. About 50% of my job is
    convincing different audiences of different things.

    For the purposes of this post, we will ignore the internal parts of my job that
    don't involve compilers. I'm not yet feeling confidently metacognitive
    enough about those bits to do *thought leadership*.

## The fast paths

We'll start with the view from the interpreter

### Static properties

Most programmers don't use most programming language features.


Look at the following Ruby code snippet:

```ruby
def add(a, b)
  a + b
end
```

As I and others have [made a habit of saying](/blog/typed-python/), in
languages such as Ruby and Python, this kind of code could do *anything*. The
surface syntax of addition is compiled to a virtual method call under the hood.
Inside the interpreter, the opcode handler might look something like this:

```c
VALUE handle_send(Symbol name, int argc) {
    VALUE receiver = stack_at(argc);
    void *method = lookup_method(receiver, name);
    return call_method(method, stack_ptr() - argc, argc);
}
```

We, as VM implementors, have additional context: most people use `+` to mean
integer addition or string concatenation[^measure]. So if we inline some very
fast checks into the method send handler, we can execute integer addition and
string concatenation much faster.

[^measure]: Citation needed, but people are on average not very creative. To
    determine if this is the case for your programming system, you must measure
    and get some statistics for the applications you care about.

```c
VALUE handle_send(Symbol name, int argc) {
    if (name == Symbol_PLUS
          && argc == 1
          && is_fixnum(stack_at(0))
          && is_fixnum(stack_at(1))
          && nobody_has_overridden_integer_plus()) {
        VALUE right = stack_pop();
        VALUE left = stack_pop();
        return fixnum_add(left, right);
    }
    if (name == Symbol_PLUS
          && argc == 1
          && is_string(stack_at(0))
          && is_string(stack_at(1))
          && nobody_has_overridden_string_plus()) {
        VALUE right = stack_pop();
        VALUE left = stack_pop();
        return string_concat(left, right);
    }
    VALUE receiver = stack_at(argc);
    void *method = lookup_method(receiver, name);
    return call_method(method, stack_ptr() - argc, argc);
}
```

These new checks, which only apply to a bytecode-compile-time known subset of
method sends, slow down the other method sends. As (for example) Koichi Sasada
noticed when creating YARV, there is no sense compiling this directly to a very
generic method call, brushing your hands off, and calling it done.

Instead, we should split out the send into multiple handlers and only have the
bytecode compiler generate this specialized addition opcode when have the right
number of arguments:

```c
VALUE handle_send_plus(Symbol name, int argc) {
    assert(argc == 1);
    VALUE left = stack_at(1);
    VALUE right = stack_at(0);
    if (is_fixnum(right)
          && is_fixnum(left)
          && nobody_has_overridden_integer_plus()) {
        stack_popn(2);
        return fixnum_add(left, right);
    }
    if (is_string(right)
          && is_string(left)
          && nobody_has_overridden_string_plus()) {
        stack_popn(2);
        return string_concat(left, right);
    }
    void *method = lookup_method(left, name);
    return call_method(method, stack_ptr() - argc, argc);
}

VALUE handle_send(Symbol name, int argc) {
    // ... as before ...
}
```

You may notice that we still have some situations for which some checks are
known at bytecode-compile-time: we may know the type of the left hand side or
the right hand side. So why not specialize those cases into their own handlers
as well?

Partially this is because it's not worth the effort: we've already specialized
addition at no cost to other method sends and your C compiler will probably do
a good job optimizing through the helper function calls.

But also, code, especially hand-written code, comes with a maintenance burden.
Are you going to manually deal with all of this[^deegen]? No, that would be a
total bummer.

[^deegen]: deegen

### Dynamic properties

There are other properties we care to check: the method override. Even if we
make this override check cheap---a load and a compare, perhaps---it's still
significant overhead for adding two small numbers.

We could instead use [quickening][quickening] (PDF) to generate specialized
versions of the opcode handlers that can assume *without checking* that integer
plus and string plus have not been tampered with. Then, only if the methods get
tampered with, re-write all of the specialized opcodes that depend on this
property to the more generic version.

[quickening]: /assets/img/ic-meets-quickening.pdf

### In a JIT

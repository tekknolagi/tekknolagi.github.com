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

Most programmers don't use most programming language features. Look at the
following Ruby code snippet:

```ruby
def add(a, b)
  a + b
end
```

As I and others have [made a habit of saying](/blog/typed-python/), in
languages such as Ruby and Python, this kind of code could do *anything*. The
surface syntax of addition is compiled to a virtual method call under the hood.
The opcode handler, simplified, looks something like this:

```c
VALUE handle_send(Symbol name, int argc) {
    VALUE receiver = stack_at(argc + 1);
    void *method = lookup_method(receiver, name);
    return call_method(method, stack_ptr() - argc, argc);
}
```

We, as VM implementors, have additional context: most people use `+` to mean
integer addition or string concatenation. So if we inline some very fast checks
into the method send handler, we can execute integer addition and string
concatenation much faster.

```c
VALUE handle_send(Symbol name, int argc) {
    if (name == Symbol_PLUS
          && argc == 1
          && is_fixnum(stack_at(0))
          && is_fixnum(stack_at(1))
          && nobody_has_overridden_integer_plus()) {
        return fixnum_add(stack_at(1), stack_at(0));
    }
    if (name == Symbol_PLUS
          && argc == 1
          && is_string(stack_at(0))
          && is_string(stack_at(1))
          && nobody_has_overridden_string_plus()) {
        return string_concat(stack_at(1), stack_at(0));
    }
    VALUE receiver = stack_at(argc + 1);
    void *method = lookup_method(receiver, name);
    return call_method(method, stack_ptr() - argc, argc);
}
```

There's just one problem:

As (for example) Koichi Sasada noticed when creating YARV, there is no sense
compiling this directly to that method call, brushing your hands off, and
calling it done.

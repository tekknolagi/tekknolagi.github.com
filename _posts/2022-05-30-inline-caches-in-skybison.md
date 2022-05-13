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

## Loading attributes

### Monomorphic

### Polymorphic

## Loading methods

### Monomorphic

### Polymorphic

## Modifying types

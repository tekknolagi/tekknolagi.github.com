---
title: "How we use binary search to find compiler bugs"
layout: post
date: 2022-08-11
description: >
  Inlining is one of the most important compiler optimizations. This post
  describes the Cinder JIT's function inliner and how it speeds up Python code.
---

I work on [Cinder](https://github.com/facebookincubator/cinder), a JIT compiler
built on top of CPython. This post will talk about how we use binary search to
track down compiler bugs, but this is a technique that is applicable to any
compiler if you have the right infrastructure.

## Motivation



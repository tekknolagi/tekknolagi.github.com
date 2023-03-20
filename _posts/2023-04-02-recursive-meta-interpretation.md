---
title: "Recursive meta interpretation with PyPy"
layout: post
date: 2023-04-01
---

* pypy is a fast python runtime with a tracing just-in-time compiler
* pypy is a big project with a lot of subprojects
* it includes rpython, a python-looking language that compiles to c
* it includes a python interpreter written in rpython
* it includes a system to transform interpreters written in rpython into
  tracing jits
* these three component parts are used primarily to create a jit
  for python
* but people have written other jits using rpython
  * lox:
    * https://github.com/cfbolz/yaplox/tree/jit
    * https://github.com/hardbyte/pylox
  * haskell:
    * https://ntnuopen.ntnu.no/ntnu-xmlui/handle/11250/253137?locale-attribute=en
  * racket:
    * https://github.com/pycket/pycket
  * php:
    * https://github.com/hippyvm/hippyvm
(below bullet might be removed because it's not necessarily relevant to the
research problem)
* this is hard, for a couple reasons
  * rpython is python2 and the world has moved on
  * the tooling is slow, which leads to long write-test cycles
  * the tooling has tricky error messages, which further exacerbates slow dev
    cycles
  * rpython type annotations do not use standard PEP 484 type hints, but
    instead assertions
(end "irrelevant" bullet)
* why not upgrade to python 3? that is a lot of work for pypy authors and does
  not solve all the other gripes
* other motivation:
  * write jits in other languages
  * avoid the slow development cycle because rpython has been cut out
  * can we go "deeper" in the implementation hierarchy without losing
    performance?
    * how much warmup do we need?
* cfbolz has written an interpreter metaslf https://hg.sr.ht/~cfbolz/metaslf
* it is several things:
  * an interpreter for a language called slf written in rpython
  * wrappers for the rpython jit bindings to expose them to the slf language
  * an interpreter for slf written in slf that uses the exposed jit bindings
  * (implicitly,) a jit for slf
* questions:
  * what is the performance of the rpython-slf implementation?
  * what is the performance of the slf-slf implementation?
  * if there is a difference, can it be removed by improving rpython or the
    client use of the jit bindings?
  * what are the most useful bindings to surface?
* further questions
  * can we make a fast jit with only a couple of bindings?
  * can we make a fast jit without any bindings---by discovering and
    transforming interpreters automatically?
    * to find loops, cfbolz proposes value profiling to watch for
      slow/never-changing values

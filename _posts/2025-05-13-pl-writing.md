---
title: "Writing that changed how I think about PL"
layout: post
---

Every so often I come across a paper, blog post, or (occasionally) video that
completely changes how I think about a topic in programming languages and
compilers. For some of these posts, I can't even remember how I thought about
the idea *before* reading it---it was that impactful.

Here are some of those posts in no particular order:

* [a simple semi-space collector][semispace] by Andy Wingo brought the concept
  of a Cheney/copying/compacting garbage collector from theory to practice for
  me. The garbage collector core[^gc-bug] in the post is tiny, extensible, and
  can be understood in an afternoon.
* [Implementing a Toy Optimizer][toy-optimizer] by CF Bolz-Tereick changed how
  I think about instruction rewrites in an optimizer. No more find-and-replace.
  Instead, use a forwarding pointer! I love union-find. The whole toy optimizer
  series is fantastic: each post brings something new and interesting to the
  table.
* [A Knownbits Abstract Domain for the Toy Optimizer, Correctly][known-bits] by
  CF Bolz-Tereick is a two-for-one. It both introduced me to a new abstract
  domain for optimizers and changed how I think about Z3. Before, I vaguely
  knew about Z3 as this thing that can check numeric operations, kind of. The
  post, however, uses Z3 as a proof engine: if Z3 can't find a counterexample,
  the code is correct[^correctness]. Furthermore, it uses Z3 as a verifier *for
  Python code* by using the same Python code on both Z3 objects and normal
  Python objects.
* [Cranelift, Part 3: Correctness in Register Allocation][regalloc-verifier] by
  Chris Fallin also made proofs more accessible, but in a different way.
  Instead of proving your register allocator correct on *all* inputs, prove it
  correct on one input: the current code. This means that in production, you
  either get a correct register allocation or a meaningful crash. Additionally,
  it uses fuzzing as a state space exploration tool that can identify bugs by
  making the verifier fail.
* [Regular Expression Matching: the Virtual Machine Approach][regex-vm] by Russ
  Cox made regular expression engines make sense to me. In the blog post is a
  small regular expression engine in under 50 lines of readable code. As a side
  effect, it also made coroutines/fibers/schedulers more understandable because
  its implementation of nondeterminism makes regex "threads" in user-space.
* [micrograd][micrograd] by Andrej Karpathy is a tiny implementation of neural
  networks with no libraries (not even NumPy). It's how I came to understand
  machine learning and even write some blog posts about it.
* [How I implement SSA form][pizlo-ssa] by Fil Pizlo changed how I think about
  union-find. Instead of either storing an additional pointer inside every
  object or having a side-table, add an `Identity` tag that can store the
  pointer. Unlike the other two approaches, this is a destructive rewrite, but
  it saves space. It also introduces two new things that I am still mulling
  over: Phi/Upsilon form and TBAA-style heap effects.
* [Speculation in JavaScriptCore][speculation-jsc] is another absolute banger
  by Fil Pizlo. I learn new things every time I re-read this post, which
  describes how JSC's various optimizers work (and I guess how Fil thinks about
  optimizers).
* [Modernizing Compiler Design for Carbon Toolchain][carbon] is a talk by
  Chandler Carruth on the design of the Carbon compiler. About 29 minutes into
  it Chandler sets *incredibly aggressive* compile-time budgets and explains
  how the compiler is architected to fit in that time budget. At around 40
  minutes, he starts explaining per-layer how this works, starting at the
  lexer.
* [A Python Interpreter Written in Python][python-python] by Allison Kaptur
  made bytecode interpreters (and, specifically, how CPython works internally)
  click for me.
* [Parsing expressions by precedence climbing][precedence-climbing] by Eli
  Bendersky presented an understandable and easier-to-develop alternative to
  traditional hand-written recursive descent parsers. It both made parsers less
  scary to me and also at the same time illustrated how precedence climbing is
  the same algorithm as recursive descent but instead of having 10 different
  functions with similar structure, it uses one function and a table.
* [Ruby JIT Challenge][ruby-jit-challenge] by Takashi Kokubun is a great start
  to code generation and it is more general than JIT, too. In the post he also
  shows off a cool approach to register allocation that I had not seen before:
  fold your stack operations at compile-time to operate on a virtual stack of
  physical registers.
* [An Incremental Approach to Compiler Construction][incremental-compiler]
  (PDF) by Abdulaziz Ghuloum brought compilers and code generation from a
  mystical multi-pass beast down to an understandable single-pass form. I like
  how it implements each feature end-to-end, one by one.
* [Lessons from Writing a Compiler][lessons-compiler] by Fernando Borretti puts
  into words this stripey implementation strategy in the first section, which
  is really neat.
* [egg: Fast and extensible equality saturation][egg] by [multiple authors]
  changed how I think about optimizers and pass ordering. Why *not* just
  generate the compressed hypergraph of all possible versions of an expression
  and then pick the "best" one? I mean, there are a couple of reasons, but it
  was mind-expanding.
* [Cranelift: Using E-Graphs for Verified, Cooperating Middle-End
  Optimizations][cranelift-aegraph] by Chris Fallin showed that e-graphs are
  workable and remarkably effective in a production compiler, even if you don't
  do the "full shebang".

[semispace]: https://wingolog.org/archives/2022/12/10/a-simple-semi-space-collector
[toy-optimizer]: https://pypy.org/posts/2022/07/toy-optimizer.html
[known-bits]: https://pypy.org/posts/2024/08/toy-knownbits.html
[regalloc-verifier]: https://cfallin.org/blog/2021/03/15/cranelift-isel-3/
[regex-vm]: https://swtch.com/~rsc/regexp/regexp2.html
[micrograd]: https://github.com/karpathy/micrograd
[pizlo-ssa]: https://gist.github.com/pizlonator/cf1e72b8600b1437dda8153ea3fdb963
[speculation-jsc]: https://webkit.org/blog/10308/speculation-in-javascriptcore/
[carbon]: https://www.youtube.com/watch?v=ZI198eFghJk
[python-python]: https://aosabook.org/en/500L/a-python-interpreter-written-in-python.html
[precedence-climbing]: https://eli.thegreenplace.net/2012/08/02/parsing-expressions-by-precedence-climbing
[ruby-jit-challenge]: https://github.com/k0kubun/ruby-jit-challenge
[incremental-compiler]: https://bernsteinbear.com/assets/img/11-ghuloum.pdf
[lessons-compiler]: https://borretti.me/article/lessons-writing-compiler
[egg]: https://dl.acm.org/doi/10.1145/3434304
[cranelift-aegraph]: https://github.com/bytecodealliance/rfcs/blob/main/accepted/cranelift-egraph.md

[^gc-bug]: There's just one small bug in that post: `is_forwarded` should check
    if the bit is 0, not 1. The correct version is:

    ```c
    int is_forwarded(struct gc_obj *obj) {
      return (obj->tag & NOT_FORWARDED_BIT) == 0;
    }
    ```

    "There is a contract between the GC and the user in which the user agrees
    to always set the NOT_FORWARDED_BIT in the first word of its objects."

[^correctness]: For some definition of correct, anyway. What does it even mean
    for code to be correct? I have a whole unit on this in [my course][isdt]
    because it's a tricky topic.

[isdt]: https://bernsteinbear.com/isdt/

I hope you enjoy the readings!

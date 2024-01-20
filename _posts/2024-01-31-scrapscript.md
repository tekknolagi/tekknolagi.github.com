---
title: "scrapscript.py"
layout: post
date: 2024-01-31
---

- What is scrapscript
  - Non-technical: it’s a programming language -- simple and different but fun to make
  - More technical: it’s a PL kind of like JSON but if JSON could be more powerful
  - More technical: dependencies and hashable dependencies / merkle trees
  - Clarity: Taylor did design work and prototype in scrapscript.js and we did scrapscript.py
- History of hearing about it and reaching out
  - Why reach out
  - Why decide to build it
- Impl language design decisions
  - What are we building
  - What’s interesting for us
  - Why Python
  - Testing strategy
  - Copied initial implementation and then gutted and re-built some of it
    - [Works because of tests](https://github.com/tekknolagi/scrapscript/commit/082e30375225394f30fd270ffdcee7f5d63173ae)
  - What is different from other interpreters/compilers I’ve built
  - What was it like teaching someone who is not in PL about building interpreters/compilers (Chris)
    - C: Fun recap of COMP 105 but also brand new. Having second shot at PL
      after not doing so well in 105 was neat. Cool doing something
      collaborative and in the open. Interesting to have a foil / someone who
      knows about PL. How to work on project without fighting
    - M: Historically not worked together on larger projects but smaller ones
      went ok. What is making this one different? We’ll never know
      - kshake
      - 100prisoners
  - A little lambda calculus (See church encodings)
- Neat things
  - Web REPL
- Things that are broken
  - Serialization (recursion/closures)
- Things in progress
  - Scrapyard using Git
  - Graphics API
  - Platforms
  - Webserver platform with database
  - Alternates
- Call to action
  - Try out the web REPL

---
title: "Another partial SSI trick"
layout: post
---

In cfallin canonicalize:

If terminator is CondBranch vNN, bbL, bbM, then we can:
* Seed bbL with a rewrite of vNN to CBool(true)
* Seed bbM with a rewrite of vNN to CBool(false)

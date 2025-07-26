---
title: "Linear scan register allocation on SSA"
layout: post
---

Linear Scan register allocation (LSRA) has been around for awhile. It first appeared
in the literature in [Linear Scan Register Allocation](/assets/img/linearscan-ra.pdf) (PDF, 1999) by Poletto and Sarkar.
In this paper, they give a fast alternative to graph coloring register
allocation, especially motivated by just-in-time compilers. (I later learned
that Poletto was working on a dynamic and kind of staged variant of C called 'C
(TickC), for which LSRA would have been quite useful.)

There's also an earlier paper called [Quality and Speed in Linear-scan Register
Allocation](/assets/img/quality-speed-linear-scan-ra.pdf) (PDF, 1998) by Traub,
Holloway, and Smith. Since this paper and Poletto+Sarkar cite one another, they
must have been talking. I originally thought this paper was meant to be an
improve on Poletto+Sarkar, but it came first. So let's call them
contemporaries.

Linear scan is neat because it does the actual register assignment part of
register allocation in one pass over your low-level IR.

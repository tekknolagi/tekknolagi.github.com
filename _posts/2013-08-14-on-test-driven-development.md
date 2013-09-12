---
layout: post
published: true
title: On test driven development
---

In July, I got contracted to write an API for an iOS app. I elected to use [Grape](https://github.com/intridea/grape) on top of [Rack](http://rack.github.io). It started off pretty manageable, with routes being fairly distinct from one another, and not relying on one another much. However, as the code grew, it became more and more difficult to track down bugs. I spent hours, sometimes days, hunting down minuscule bugs in the database code, database interactions, in everything. Bugs started appearing out of nowhere as I added new features. I decided I had enough, and wrote some tests.

I had never written tests before. I'd implemented some breakpoint-type-things that would print out the status of different variables, but never formal unit tests. Despite reading article after article on Hacker News and r/programming about how tests were absolutely essential, I ignored them. Big mistake.

I wrote tests to cover every outcome for every API route. It was lovely. Seeing green dots pop on the screen was like magic. Tests were fantastic. But... what then? Write a route, write a test, move on? I'd heard of something called Test Driven Development, where the developer will write tests to cover the outcomes he *wants*, then write a route to match that. Before bughunting like crazy, that sounded like nonsense. Why not just go ahead and develop, maybe write tests later? A couple reasons:

1. Test writing can be *exhausting*.

2. If no formal "spec" exists on paper, it is difficult to verify that the code works as intended.

I started writing tests before routes. Everything got easier.

I'm probably not going to drive *all* of my development with tests, but for large projects it eases a lot of pain.

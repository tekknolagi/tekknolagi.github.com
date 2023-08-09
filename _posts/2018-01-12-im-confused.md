---
title: "I'm confused"
layout: post
date: 2018-01-12 19:19:27 PST
---

I am a Computer Science Teaching Fellow at Tufts. Every so often, somebody
comes up to me and expresses some form of the same kind of anxiety,
frustration, worry --- whatever you want to call it --- centered around one
thing: _"Oh gosh. Everyone here knows more than me and they understand things
so quickly and they solve problems so fast."_

First, I want to address the comparison to other people. Don't do that! You are
a different human, a different mass of cells and meat and fluids, with a
different background, a different history, and a different brain.

Second, I want to address the notion that everyone knows more than you. Other
people will be better than you at something. You will be better than other
people at something else. This is normal. This is expected. We all go to school
to learn and become better. Learning to become okay with this feels like a
superpower. I am still working at it.

Third, I want to tell my story.


### I am not special.

I am the product of many happy coincidences. I wrote my first line of code when
I was around the age of nine. For some reason, people attribute a lot of
meaning to that, but my interest in programming all comes down to weirdly
specific and lucky combination of factors:

* I did not play videogames, but somehow I wanted to make them
* I had the wherewithal to ask my fourth grade teacher how to make videogames
* He had enough patience to go home and poke around on the internet for a bit
  and recommend Scratch
* My dad bought an [OLPC]
* The OLPC had a small Python-esque interpreter called [Pippy] installed
* I had enough time to sneak down and use the family computer at night when
  nobody was monitoring it
* We traveled back to California every once in a while
* Our neighbors in California hired a tutor named Steve[^jobs] to tutor their
  son in computer science and math
* Steve was kind enough to sit me down and teach me alongside my neighbor

Without any one of those having occurred, I might not have started to write
code in the first place. Which might have been better for my long-term mental
health, because I consistently frustrate myself trying and failing to
understand concepts or complete projects.


### I have been confused for ages.

_What follows is a half-complete log of me trying and failing to understand how
programming works. It spans eleven years and much heartbreak._

I started playing with Scratch. I tried to make some games. I did not
understand how boolean logic and loops and time worked, so instead I made some
short animated movies with the block syntax.

I found Pippy, a small and custom Python interpreter for the OLPC, and made
some weird program that told bad jokes. I do not remember the specifics, but I
had at least figured out `if`-statements and reading input by then. It probably
took me a month to figure out why `input` would sometimes work and sometimes
give a syntax error. Nobody was around to tell me that `raw_input` was the way
to go, because `input` called `eval` on whatever the user entered. And I would
not have known what `eval` meant anyway. Nothing much came of my OLPC
programming.

I discovered that the crappy calculators we used for middle school math were
programmable in an obscure language called TI Basic. I heard they could be
programmed on the computer and then that program transferred to the calculator.
But we did not have the cables, so instead I spent many math classes trying to
type full programs on the little keyboard. I wrote some helper functions that
did some trigonometry problems for me. I did not understand what I was doing,
though, or how variables worked, so whenever I wanted to write a new program I
would have to go through the manual again and try and discover anew how TI
Basic worked.

I remembered Pippy and tried in vain to find a Pippy interpreter for Windows
XP. Alas, nobody wrote such a thing --- of course --- but I somehow stumbled
upon Python and fumbled my way through a Python/IDLE installation. I wrote a
math module that did more of my math homework for me. But my attempts to do
anything remotely beyond that scope were foiled. And my math teacher found out
about the module because my homework had all these unnecessary steps in them,
and my program was not smart enough to remove them. So I got in trouble.

At some point in here I discovered JavaScript and spent a couple days writing
small interactive websites. In order to do some kinds of logic, the internet
recommended writing code in PHP. So I spent several weeks not understanding the
difference between client-side and server-side applications, trying to embed
PHP in a `<script>` tag. I eventually gave up. I only grasped the difference
years later.

Steve, the tutor from some time ago, tried to teach me something about
programming on a semi-regular basis. He used [SICP] as a text resource and had
me learn both in C and in Scheme. I could repeat what he did, but I could not
come up with the solutions to these problems on my own. I got really frustrated
because I could not understand what the heck *recursion* was and why it was so
important that a function could call itself.

In one of my English classes in high school, there was a time when the class
was sent to the computer lab to write an essay. A friend of mine was writing
something that was decidedly *not* an English essay, so I inquired further. It
turns out that he was writing a Python program to trade Bitcoin.[^bitcoin] He
agreed to (re-)teach me the basics of Python, because I remembered
next-to-nothing. I wrote some bad code and called it a day.

I joined the robotics team because my aforementioned neighbor was on it and he
was the programming captain. He was the only programmer on the team and I
wanted to be of service. At this point in his career, at the age of 15, he had
already written a fully-functional parser, compiler, and virtual machine *in
C*. Because I was functionally useless and also wanted to learn how to make my
own programming language, he sent me away several times with exercises like
"write a simple stack-based math language".[^rpncalc] I coded myself in circles
and did not grasp how the program was supposed to maintain state or even
properly read in input.

I took the AP Computer Science class that my high school offered. The exercises
were generally reasonably trivial and they were partner exercises. The hardest
one for me and my partner was implementing Dijkstra's algorithm. We did not
figure it out in time and submitted a half-baked and broken solution. To this
day, I cannot recall how exactly it works without referencing some external
source.

Sometime during high school, I got so frustrated that I came nowhere close to
understanding programming at the level that my neighbor did. I [turned to the
internet][StackOverflow] for help. Some kind soul named Charles[^charles] took
the time to write a long, well-thought-out response. I did not really
understand what he meant at first, but after a month or two of goofing off when
I should have been working at my summer job, something clicked and I wrote a
small program that accomplished part of his suggested task. It was not
Turing-complete, but it *worked*.

Another English class came along. In this one, my teacher had an idea of "20%
projects", the kind that Google encourages. One day a week, we got to work on
projects of our choosing --- so long as we wrote them up for the end of the
semester. I chose to re-write my crappy "programming language" as an actual
virtual machine, because I still did not understand how on Earth those were
supposed to work. I had a half-working implementation by the end of the
semester, and continued poking at it throughout my freshman year in college. I
was fortunate enough to have a mentor --- my robotics coach --- when everything
frequently went to hell and broke. He was there primarily for moral support and
occasionally to point me in the right direction.

There is too much frustration in college to post it all here. It would sound
repetitive. But I struggled in COMP 15. I struggled in COMP 40. In Web
Engineering --- I think I pissed off everybody I worked with in my group. I
struggled in Operating Systems. In COMP 160. In COMP 170. I *still* do not know
what happened in Graphics (sorry, Jason!). Or the Security Capture the Flag,
for that matter (sorry, everyone on my team!). I struggled in COMP 105. In
Networks. I struggled as a TA. I only figured out how the heap data structure
was supposed to work when I got up to lead a lab about it and someone asked a
particularly tricky question. I answered student questions wrong. I told them
too much. Too little. Did not know how to teach. Did not know how to manage my
own time.


### I will continue to be confused.

Even now, I am bashing my head against the wall trying to figure out how I want
to represent my data for the compiler I am working on. Some people react
strongly to this --- "Max, but you at least understand compilers! You did XYZ
thing!". Sometimes, sometimes not. It does not *feel* like I understand what is
going on. After years of trying and taking three separate classes about
interpreters and compilers, I have not written a functioning compiler. And this
occasionally scares me because I am supposed to work on compilers full-time
next year.


### It's okay.

I spoke to somebody at Google who studied compilers in undergrad and got a
Master's degree in compilers. He confessed to me that he too has not written a
compiler and sometimes worries about this. This helped me realize: I am not the
only one who feels like a fraud!

I spoke to my friend and co-TA Margaret, who reframed my thinking. She
explained that it is important to think in terms of *deltas*. That I should
look back over the past years and realize how much I have learned and
accomplished in that time. A year ago, where was I? I was writing a blog series
about interpreters because *I did not know how they worked and needed to
learn*. And through writing about it I --- over the course of half a year ---
finally figured out how the heck Lisp worked.[^macros]

There are so many wonderfully talented people who I know and admire. They
suffer from the same feelings of inadequacy that I do. That many people do. I
have occasionally been part of the reason for these feelings. I want to open up
and make it clear that I did not learn how to program in a vacuum and certainly
do not feel like I know what I am doing most of the time. I'm confused. And
that's okay.

<br /><br />
This post resulted from a conversation with [Margaret Gorguissian][Margaret].
Thank you so much!

[OLPC]: https://en.wikipedia.org/wiki/One_Laptop_per_Child
[Pippy]: http://wiki.laptop.org/go/Pippy
[Margaret]: https://teragr.am
[SICP]: https://mitpress.mit.edu/sites/default/files/sicp/full-text/book/book.html
[StackOverflow]: https://stackoverflow.com/q/6887471/569183

[^bitcoin]:
    As it turns out, this is how I discovered Bitcoin. The year was 2011 and
    Bitcoin was worth next to nothing. This is a sad story of its own about
    writing but never launching a Bitcoin trading bot. Then buying at $4 and
    selling at $7.

[^rpncalc]:
    Interestingly enough, this is an exercise that we now give students in the
    second introductory course at Tufts. I really enjoy that I have come full
    circle in that regard.

[^jobs]: Not Steve Jobs. Somebody asked me this.

[^charles]:
    Because life is funny that way, Charles and I ended up briefly working at
    the same place. We only realized when I tracked him down years later to
    thank him for the answer to my question.

[^macros]:
    Sort of. I still have not figured out macros.  Which is sad, given that I
    worked on an elaborate macro system as part of my last job.

---
title: "Travel notes: PLDI Boulder"
layout: post
---

I had another excellent [PLDI](https://www.sigplan.org/Conferences/PLDI/) this
past June. It was my fourth. I continued to meet new people and learn new
things!

Overall: I got to meet a lot of new people, which was exciting. I had some good
chats about research. I asked a question at a talk! I got to show Aaron and
Jacob PLDI and see them enjoy it. I missed hanging out with CF Bolz-Tereick and
Chris Fallin, the usual suspects at conferences I attend. I'm looking forward
to next year.

This post is more about the conference than the town of Boulder (unlike the
last PLDI post about Seoul) because I didn't do much Boulder exploring.

## Sunday

I got in late on Sunday. Then I had to take a long car ride from Denver airport
to Boulder. I don't think I had ever flown into Denver with intent to go to
Boulder before so it was a bit of a surprise.

Jacob offered to have a late dinner with me so we had a tasty meal at Gaia
Masala and Burger. Shout out to Harry, our server.

## Monday

Monday was a workshop day. I signed up for EGRAPHS and mostly stayed in that
workshop. People kept throwing around the term "Knuth-Bendix", as they have
been for several years, and only in one of these workshop talks did someone
explain it in a way that made any sense at all. It seems kind of like equality
saturation but for the rewrite rules themselves---no actual expression graphs
involved. I DMed Phil this sketchy explanation during a talk to get his
response and I got to watch him cock his head and think about it in real time.

At lunch I met Qiantan Hong and we got to talking about Common Lisp and its
object system, CLOS. Seems like a combination of ahead-of-time compilation and
multiple dispatch is really tricky.

I had dinner with Aaron at Postino and then wandered into a bunch of people
staying at the conference hotel chatting in the lobby. Ben Titzer said "fix my
subtyping bug", which I interpreted as him saying hi. I ended up just planting
myself at the table as a bunch of interesting people cycled through: Jared
Roesch, Mae Milano, Hila Peleg, Russel Arbore. It was a late evening.

## Tuesday

Back to the workshops! But late because of aforementioned late evening. I saw
Vadym's talk about Remora. I only understood about 40% of it but it was good to
catch up. I hadn't seen him since leaving Northeastern.

Around a break time I joined a little cluster of people talking about e-graphs
and I guessed asked enough basic questions that Pavel convinced Max Willsey to
run a "BYOEG" (build your own e-graph) tutorial. The structure was as follows:
Max would instruct Jacob as to what *kind* of thing to build next but not be
prescriptive about exactly how to build it and not look at Jacob's screen. The
rest of us would sit around a table and try to follow along as best we could. I
hear Pavel has a blog post about this experience coming soon...

I saw Slava Pestov walking around and introduced myself because we keep liking
one others' bad jokes on Mastodon. We ended up getting dinner with Aaron and
Jacob that night at Leaf. We learned a lot about monoids, Knuth-Bendix (!),
Factor, and Swift. Slava volunteered to do a similar follow-along "BYOKBC"
(build your own Knuth-Bendix completion) tutorial the next day.

## Wednesday

First day of the conference! I was walking into the hotel in the morning and I
had made it about three feet onto the property when Alexa VanHattum, who was
going the opposite direction, convinced me to instead get coffee elsewhere. We
had a nice catch up and I got to hear about what teaching is like these days.

Lunch was fun. I got to do another round of "ambush person whose research I
admire" and plopped down with Ben and Christian Wimmer. I'd spent a lot of time
struggling with Christian's papers on linear scan, then convinced him to chat
about register allocation with us on a video call a couple of months ago. We
continued some of that at lunch but then I (kind of accidentally kind of on
purpose) got Ben started talking about Sea of Nodes and how it is and is not
different between Java and (for example stand-in for dynamic languages)
JavaScript. Apparently he is thinking about a similar thing that he is calling
Sea of Variables.

We talked about inlining challenges and how to infuse profiles with call
context, which can be a challenge. I feel more inspired to get type-based alias
analysis working in ZJIT.

I tracked down Christian later in the courtyard and got to hear about what he's
working on these days. I know very little about ML compilers and ML hardware
and things like that so hearing about the challenges was neat. Yannis
Smaragdakis joined our little standing table chat and we got to learn about
Datalog. Because I had previously written about linear scan register allocation
and about liveness analysis with Datalog, I goaded him into pairing with me on
writing a full linear scan implementation in Datalog.

This ended up taking the rest of the evening and several beers and then a lot
of the next day! And after the first bit of code I did not manage to contribute
very much at all.

## Thursday

I met Hannah Gommerstadt and we got to chatting about bikes and formal methods
(separately). Slava walked by and I got to introduce them. Then Jacob too.

I continued pairing with Yannis but remained really lost. The only thing I
think I contributed was some familiarity with the core algorithm, which he had
only really seen in passing before. Eventually he got it fully working, but it
needed some deep trickery. More on this soon in its own post.

I saw a talk about versioned e-graphs and that got me wondering if their
implementation can be used as a persistent e-graph or even just persistent
union-find. Sometimes you want to do backtracking, or have undo-redo in your
compiler.

Then I went to a talk about streaming byte-pair encoding (BPE). BPE is hard to
do streaming because it definitionally requires looking over the whole input
string. They did some neat trickery to find boundaries in the string that
demarcate regions that don't interfere with one another and thereby tokenize
on-the-fly. I didn't understand it fully but I asked my only question of the
conference, which was if this could also be used to implement BPE in parallel.
Seems the answer is "maybe" so I should probably reach out and ask further.

Slava started showing me and Jacob and Aaron how to implement Knuth-Bendix
completion for strings. I had a lot of tiny little bugs which slowed progress.
Such is life. The banquet and awards ceremony started so we called it a night
and went off to eat dinner. They had good lentils.

## Friday

I ran into Thalia Archibald and John Regehr and we talked about (really, they talked
about and I tried to learn something) what it might mean to either port Alive2
to another compiler than LLVM, or build "Alive3", or "Mini-Alive" for some
other IR. John suggested fuzzing the hell out of the thing first, then doing
something more formal later... especially if it's a dynamic language IR where a
lot of the opcodes end up being "function call that can do anything".

I had a nice chat with Steven Holtzen and Zach Tatlock about research and grad
student life. I got some good advice. I meant to talk to Zach about this thing
we keep occasionally chatting about that I call "the big e-graph in the sky". I
talked a bit about it to Max Willsey and he had some good probing questions
about what would be slow, challenging, or somehow undefined given my problem
statement.

I continued struggling to implement Knuth-Bendix with significant assistance
from Slava and I think eventually got *something* working. I had a really nasty
bug due to string slicing semantics in Ruby.

Aaron went off to learn about deep immutability in Python and then got to
chatting with the authors of the paper. We got to compare notes about language
and language implementation challenges. It's been a long time since I was in
Python-land.

Aaron and Slava and I got enchiladas for dinner.

## Saturday

I had no reading material for the flight so I went downtown, intending to buy
one book, but got too many books. They barely fit into my bag for the flight
home. I started reading Anathem for the second time. It holds up. It's a damn
good book.

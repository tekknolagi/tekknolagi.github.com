---
comments: true
date: 2012-10-27 17:23:28
layout: post
slug: meet-brightswipe
title: Meet Brightswipe, a new torrent indexer
wordpress_id: 1250
categories:
- general
---

Since I posted in July about the private torrent indexer, I've been working on something called Brightswipe. [Brightswipe](http://brightswipe.com) is a beautiful, fast, and open-source indexer written in Ruby/Sinatra.

I chose Sinatra because I wanted to learn Ruby, and thought Rails was too clunky for this small of a project.

Originally, the entirety of Brightswipe was in one 200-line Ruby file with no styling at all. As the project progressed, however, I decided instead to segment it, putting the views in their own `views/` folder.

Still, though, `indexer.rb` was too long and complex, so I moved the `configure` block and functions out into their separate files.

I tried to add a second database option — MongoDB. Mongo, unfortunately, its not suited for the structure I chose for the DB. Halfway through adding in all the code, I decided that it was not worth it at all.

I then ditched both and decided to use DataMapper with MySQL.

One day, I was reading through Hacker News, and a guy by the name of Michael Mettler ([card.io](http://card.io) co-founder) was freely offering 10 domain names that he no longer needed. Among the list, I spotted a domain that I liked: `brightswipe.com`.

I wrote a comment on the post, explaining how I already had written an application and just needed a designer, and gave him my email address. Within a few hours, he'd emailed me to tell me that I'd gotten the domain, and just to start the transfer process.

I happily complied. Within another few hours, I got an email from a guy named [Ashraful](http://madebyargon.com). He said that he'd seen my comment, and was willing to design for me. I told him that I had a $0 budget, but would happy to code for design.

He agreed, and within a week I had a running design for Brightswipe:
![](http://wp.bernsteinbear.com/wp-content/uploads/2012/08/brightswipe-home-1024x619.jpg)](http://wp.bernsteinbear.com/wp-content/uploads/2012/08/brightswipe-home.jpg)

It turned out that Ashraful wasn't so great with CSS, so all I had was a PSD and an image render. I'm not great with CSS either, unfortunately.

I decided to contact another designer named [Ege](http://egegorgulu.com). I explained my predicament, and my lack of budget. I told him that he'd get his name in the footer of the site (along with mine) when it went live. He agreed to help out with translating to CSS.

The site goes live today. I'm posting about it on Hacker News and Reddit, too. Please, comment with your feedback!

P.S. The [code](http://github.com/tekknolagi/indexer) is on GitHub!

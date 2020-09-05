---
comments: true
date: 2012-09-20 20:10:31
layout: post
slug: a-quest-for-anonymity
title: A quest for anonymity
wordpress_id: 1294
---

With all of the recent laws and acts that try to nullify internet privacy and anonymity, it's beginning to seem impossible to remain anonymous. However, with all of the tools available to people today, it's easier than ever. It just requires consistency and a decent amount of intelligence.

Most people concerned about anonymity simply won't post personal details — but that isn't enough.

You see, there's one problem: when registering for anything, the service you use could have stored your IP address, or other identifying details. IPs can be used for geolocation, so your general whereabouts would be compromised. Especially if the government raids the blogging service you use. Bad news.

Most people know how to use a proxy, or at least what a proxy is — and that's great — but it's not the best way to hide yourself. Think about how your browser is configured, what OS you have installed, and the fonts you have installed on your computer... that's a pretty unique combination, right? Especially if you consider cookies, and other browser data. What you have is almost a universally unique computer, unless you have a completely vanilla install.

There's also the problem of packet sniffing, so unless you're using SSL or are tunneling over a proxy that uses some form of encryption, anybody on your network can read and intercept your packets. Also bad news.

This is why it's considered difficult to be completely anonymous, but if you try hard enough... you can get close. Let's talk strategy.


### Anonymizers


**Proxies**

Proxies are great tools, but limited in their capabilities. They can be explained as follows, in human terms: You're a shy person, but have this really outgoing friend. You want to ask the teacher a question, but are afraid to ask. He volunteers to go for you, talks to the teacher in private, gets the response, and then tells you about it in private later. All through that process, nobody found out who you were — but your friend had to do all the dirty work. In this example, your friend is the proxy, and you're the computer hiding behind it.

Many public proxies float around the internet, allowing you to proxy over HTTP, HTTPS, SOCKS, or what have you. These proxies are often untrustworthy or slow, and are therefore poor choices. If you've a friend in another country with a decent internet connection, you can easily create an SSH tunnel and proxy through that. Just ask your friend to open up port 22, make you a username/password, and funnel traffic. Naturally, there's the trust that you won't do illegal things with his internet... but somebody has to trust you.

**Tor**

Tor is a curious creature. It's a bit of software that allows users to forward their web browsing through random layers of computers, confusing the heck out of anybody that wants to trace an internet request back to you. It's essentially a network of chained proxies. Let's take a look at this diagram.

![](/assets/img/uploads/2012/09/how_tor_works.png)](http://wp.bernsteinbear.com/wp-content/uploads/2012/09/how_tor_works.png)
Courtesy of torproject.org



This is the second in a series of diagrams showing how Tor works. It explains how Alice, some generic user, wants to request a page, and does so through a front of computers. Is there a way to trace it back to her? Yeah, I suppose, but it's complicated. Very complicated. Tor is a great tool for political dissidents who want to get around firewalls, so naturally it's perfect for the average internet user to escape being found out. With this method of requesting pages, traffic is _bound_ to be very slow... so don't watch movies or torrent through it. That's not the point.


### Practices


**Encryption**

If you're talking to somebody and you don't want it seen, there's no better way than to just encrypt your conversation. Use GPG in your email; just ASCII-armour your message and paste into the body. Of course, you need to exchange public keys, but that's not hard. I'm actually writing a small GPG email client with a GPG address book built in. I'll release it on GitHub when it's done.

**Public WiFi**

No, this isn't a joke. Never blog or log into email from home; it's too risky. Go through whatever anonymizing software on TOP of an internet connection that's not yours. This part is inconvenient, but it's hard; there are plenty of libraries and coffee shops around. There's absolutely no connection to your home/IP, then. If you must, you can use your own computer.

**No linking accounts**

You should make no connection whatsoever between your new identity and your real one. Kind of defeats the purpose. That means no logging in with Facebook, your old Gmail, talking about your hometown, or anything. Nada. Make sure it's completely separate, and nobody would be able to associate your username or email with you in real life.

**Secure passwords**

I'm not going to preach password entropy or anything of the sort; I've limited knowledge of that. Find a good research article that talks about password security, especially against rainbow tables, and check out XKCD's [strip](http://xkcd.com/936/).

**Keep a log of services you use**

This is something I found useful when starting fresh, anonymously. It constantly reminds you of what you're saying, and where. If somebody where to collect all the information on your various accounts (that you'd maybe forgotten about), you might be compromised.

**Everything free**

Don't pay for stuff. Your payments can be easily traced to you. How do you get free stuff? Like this:

You need a new email address. You'll use this email to sign up for services anonymously. You _cannot_ use a handle (screen-name, or username) that has anything to do with your identity. You also can't host your own email; you'll run into the payment problem (explained below). Worry not; there are many free services out there, like GMail, Hotmail, Outlook.com, mail.com, hushmail.com... the list goes on.

Let's assume you want to write a blog. You have many choices, but know for a fact that you _cannot_ pay for the service you use, especially not a domain. Think about it: your credit card and name are attached to everything you buy over the internet. Let's pick a free option: Wordpress, Blogger, Blog.com, tumblr, soup.io... and more.

If you want to have your own website without a preset blogging platform, you can look at places like FreeHostia, AppFog/PHPFog, and other places that offer a free (and limited) plan. Subdomains are fine. They look hacker-ish too, I suppose.

**Minimal storage on your computer**

Don't keep anonymous/incriminating stuff on your hard drive. Just don't. It's an accident waiting to happen, and only speeds up police raids (if, say, you're a dissident). It's much harder to force access to a hosting provider, since it could be across borders, and there's no physical computer to "hack". And please for the love of all that is holy do **not** save your passwords in any browser or text file. This also makes it easy to have an easy way to jump ship.

**Emergency button**

If everything goes to hell, if you've been found out, if you need to disappear... know that you can't completely do that. Chances are, if you've been found out, people have copied stuff from your site, or taken screenshots, and published elsewhere. Either way, delete everything. Close your email account, your blog, and all the services you should have been logging.


### Conclusion


Be safe. Don't do stupid stuff. It will come back and bite you in the ass.

**Edit**

You can definitely go to a drug store and buy a prepaid credit card with cash, or a SIM card for a phone. I suppose you can pay for things, but it's still riskier than free stuff. Having an anonymous phone would be awesome.

**Edit 2**

Eric Carrell reached out to me and suggested that I link to [this blog
post](https://www.cloudwards.net/how-to-set-up-a-strong-password/) about
password strength. After a brief read, I think it's a nice suggestion. THanks,
Eric!


### Addendum


Reddit user elebrin noted that I could have covered more on encryption, anonymous payments, and header spoofing, so here I go.

**Bitcoin**

Bitcoin is a peer to peer internet currency backed solely by computational power. That's probably the most basic definition, but if you want to read up on it, check [here](http://bitcoin.org/).

It's a great way to make payments anonymously, as it doesn't require a credit card or anything of the sort. If companies accept Bitcoin payments, then you need not compromise your identity for the sake of paying for a server.

**Truecrypt**

Truecrypt is a tool that enables you to encrypt and decrypt files on the fly, as if they're normal files. This is perfect when you just need to use it on a flash drive. It also has a feature called a "hidden container", whereby you can hide a block of encrypted information in a larger file. It's practically invisible.

If you want to store files in the cloud for free, like with Dropbox, just upload a Truecrypt file. Of course, Dropbox tracks IPs... so that's not a great idea.

**Opera**

Nah, this isn't about singing. It's a web browser that does many things, including allowing the user to alter the header, more specifically the user agent. This can further obscure your identity.


### Further reading


**Anonymity**
	
+ [slashgeek.net](http://web.archive.org/web/20150407072014/https://www.slashgeek.net/2012/06/15/how-to-be-completely-anonymous-online/)
+ [jaywhale.com](http://web.archive.org/web/20150707215228/http://www.jaywhale.com/how-to-make-an-anonymous-blog)
+ [wikipedia.org](http://en.wikipedia.org/wiki/Anonymous_blogging)
+ [boingboing.net](http://boingboing.net/2011/11/15/howto-be-more-anonymous-in-you.html)	
+ [metafilter thread](http://ask.metafilter.com/95483/How-can-I-host-an-anonymous-Wordpress-blog-and-not-get-unmasked)
+ [eff.org](https://www.eff.org/wp/blog-safely)


**GPG**
	
+ [Complete tutorial](http://www.dewinter.com/gnupg_howto/english/GPGMiniHowto.html)

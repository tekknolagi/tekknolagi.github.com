---
comments: true
date: 2012-10-29 06:28:59
layout: post
slug: the-perfect-linux-distro
title: The perfect Linux distro
wordpress_id: 1429


---

I recently found the subreddit [/r/DistroHopping](http://reddit.com/r/distrohopping), and was subsequently made a moderator. This brought me back to the days (well, six months ago) when I would hop almost daily between distributions, trying to find one that works for me.

I never actually found the "perfect distro" that I needed, but hopefully from my analysis here I can help myself and others.

I started to use Ubuntu back in the days when Ubuntu 6 was hot stuff:![](/assets/img/uploads/2012/10/Screenshot.png)

This was my first exposure to Linux, coming from Mac OS and Windows XP/ME. It was 2008 or so, and I was vacationing in California with my family (we used to live in California, moved to Connecticut, kept the house, and eventually moved back). Our neighbors had a tutor over who was teaching programming. I decided to jump in on the class. He required that we work in a Linux environment, so I installed the latest Ubuntu in a VM.

It was awesome; the menus were all logical and easily accessible, and for some strange reason, the UI looked amazing to me, and still does. I suppose I prefer GNOME2 to GNOME3 universally.

I migrated to the command line almost exclusively, and became a huge fan of aptitude. Its interface was simple, and all I really did was install and remove packages.

Either way, a pretty (but functional) UI is a big thing for me. I don't want to be working, staring at an eyesore. So let's keep that in mind... there should be a relatively simple UI, but things should be within reach, and preferably keyboard accessible.

After some time with Ubuntu, I got sick of looking at it, and moved to Fedora. Fedora was coming out with version 11 at the time, I think:![](/assets/img/uploads/2012/10/fedora_screenshot.png)

The blue color scheme was refreshing... at the time I did not know about skins and theming. The switch to Fedora was for no reason exciting except for the novelty of a new OS. I learned to use yum with ease, and then the fun was over. It became in essence the same as Ubuntu.

Of course! They both used GNOME2. I switched to KDE, but found my hardware couldn't handle it. Regardless, I didn't much like my OS looking "pretty" like that, especially if I wanted to take myself seriously.

A problem arose: neither of these OSes is programmer-centric. Everybody thinks, "Oh, just use some distribution of Linux! They're all for programmers." But they're really not. Linux is great for development, but there is no OS (that I've found) that is perfectly programmer-centric: console always at the ready, system monitoring in the background, minimalist UI, and a hacker "feel."

I stopped using Linux for a few years, just programming in whatever Unix-based environment Mac OS X has. I moved to Palo Alto from Greenwich, Connecticut. Joined the robotics team. Became a lot more involved in anything and everything technical. Acquired a six year old HP Compaq, and installed Linux Mint. ![](/assets/img/uploads/2012/10/Linux-Mint-13-RC-Cinnamon-Screenshot-Tour-20-1024x576.jpg)

For our onboard robotics laptop that was going to be used for image processing, we installed Trisquel. ![](/assets/img/uploads/2012/10/linux-screenshot-trisquel-5-5-03.jpg)

I was happier with Mint than with Trisquel; Trisquel wasn't as familiar, was a tad too "beautiful" for my taste, and didn't feel like a solid distro. I don't really know what I mean by solid, but it didn't feel serious enough for me. It's the reason I can't do development on Mac OS X. Mint felt like a prettier Ubuntu with GNOME2.

I guess the OS really doesn't matter; one can fairly easily adapt to the different package managers, figure out where some configurations are stored, or find binaries that will work for you. Even then, I could find an apt port for Fedora.

What matters, as far as looks, is the DM/WM combo. Everything else is pretty consistent.

Put concisely, a programmer's ideal environment would be as such (in no particular order):
	
    * Terminal always at the ready
    * Minimalist UI. Not overly pretty, but not as spartan as, say, Fluxbox or TWM.
    * Completely configurable UI, but configuration should be optional for the love of all that is holy. This is the reason I'm not particularly fond of installing Arch.
    * Emacs/Vim as an IDE — built-in compile and testing tools, as well as some basic functionality to compute runtime complexity of some functions.
    * Fast boot and log-in times.

Right now, my laptop has a good amount of those. Crunchbang was great for the stats - there was some awesome Openbox config.

And that brings me to a failed project of mine and Sidhanth's - eos. Eos was a debian-based OS targeted at programmers with all of the aforementioned features... but development stalled as our school loads increased, and we never got to finish. We'll probably upload what we have to GitHub sometime.

I'm running the latest Fedora with Cinnamon.

Good luck in your search.

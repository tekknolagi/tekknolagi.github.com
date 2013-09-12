---
comments: true
date: 2012-01-16 16:15:03
layout: post
slug: replicating-a-webfaction-site-on-tor
title: Replicating a Webfaction Site on Tor
wordpress_id: 980


---

If you have a website hosted on Webfaction (or any other name-based virtual host, for that matter), and want to create a .onion copy of that website, then this guide is for you. After the break!



First, let's get some definitions straight.

**name-based virtual host:** One user has a `/home/` partition. Each application(could be multiple domains) routes to a specific folder in `~/webapps/`.

**Tor:** Service that allows your web traffic is sent through around three random computers before reaching its target, in order to preserve anonymity. Illustration (click to enlarge):

![](http://wp.bernsteinbear.com/wp-content/uploads/2012/01/Tor-HTTP-Transmission-300x113.png)

**.onion:** A domain that is resolved using a series of queries on the Tor network.

What we'll be doing is pointing all of the Tor traffic that comes to the .onion to the daemon (in my case nginx) on the server.

I first contacted the Webfaction support staff to ask if this was allowed. I had to clarify several things:


  1. I would not be needing a dedicated IP address

	
  2. I would not be opening a port

	
  3. This will not overload Webfaction with a barrage of traffic


He (Ryan S) then OK'd my actions. I downloaded the source package from the [Download page](https://www.torproject.org/download/download.html.en), under "Source code."

I unpacked, then ran:
    
    cd tor_folder/
    ./configure --prefix=$HOME
    make
    make install


I now had a Tor binary in `~/bin/`. Right.

I then needed to make a directory in which Tor could store some of my website configuration files. I made it in `~/tor_config`.

I then edited `torrc` in order to configure the node correctly. That troublesome file was in `~/etc/tor/`.

It looked like a jumbled mess of code, I scrolled through until I found something that looks like:

    
    HiddenServiceDir some_folder/
    HiddenServicePort 80 some_IP:port


Change the first field, `HiddenServiceDir`, to match the folder from above. The second part was a bit trickier.

If you know what port and IP your server is running, then fantastic! Just leave the 80, and plug the rest of the information in.

(PSST! If you're on Webfaction, it's this:)

    
    HiddenServiceDir some_folder/
    HiddenServicePort 80 localhost:2480


Otherwise, you have two options:
	
  1. Start up another httpd, like `lighthttpd`, in the directory where your application is stored, and change `some_IP:port` to `localhost:whatever_port` to match that.

  2. Contact support and figure out what you can do


I opted to contact support, to reduce strain on the server.

I started Tor by running:

    
    ~/bin/tor


And the process spewed out some stuff like this:

![](http://wp.bernsteinbear.com/wp-content/uploads/2012/01/Screen-shot-2012-01-16-at-4.15.23-PM-300x80.png)

Then, I opened up my Control Panel (Webfaction's unique panel), and configured my BernsteinBear website to also serve my .onion domain. I just added a domain (`.onion`)
    
    cat ~/path_to_tor/config_dir/hostname

To see what's in it.

Have fun!

P.S. If you really want to take this forward, change your site root to your .onion domain!

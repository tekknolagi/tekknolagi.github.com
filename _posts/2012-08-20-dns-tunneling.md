---
comments: true
date: 2012-08-20 22:39:46
layout: post
slug: dns-tunneling
title: DNS tunneling, or, how to get around Gogo
---

We all hate those paywalls that companies put up in airports, airplanes,
lounges, or what have you. Fortunately (and thanks to
[kryo.se](http://kryo.se)), there is a tool that can get you around that. This
is a concise and easy to follow tutorial on how to set a tool called `iodine`
up.

If you're familiar with the terminal, this should take about five minutes.

**Requirements**

* 1 computer on which you have root privileges

* 1 server (with ports you can open) on which you have root privileges Control
  over the DNS of your server's domain (or subdomain — see
  [freedns.afraid.org](http://freedns.afraid.org/))

**Steps**


1. Visit [code.kryo.se/iodine](http://code.kryo.se/iodine/), and figure out
   what package you should install, based on your OS. I'm running Ubuntu
   12.04 on my server and Linux Mint 13 on my client, so on _both_ I used:

   ```
   sudo apt-get install iodine
   ```

2. Open up the zone file or DNS control panel for your server and set up an
   NS record like this:

   ```
   proxy		IN	NS	serv.mydomain.com.
   ```

   Where `mydomain.com` is some domain you own, like `bernsteinbear.com`, and
   `serv` is the subdomain of your choice. It'll serve as your DNS server.
3. Set up an A record for `serv.mydomain.com` to point to your server's IP.
4. SSH into your server and run:

   ```
   sudo iodined -cP YOUR_PASSWORD 10.0.1.1 proxy.mydomain.com
   ```

   If it runs correctly, you'll see output like this:

   ```
   Opened proxy0
   Setting IP of proxy0 to 10.0.1.1
   Setting MTU of proxy0 to 1130
   Opened UDP socket
   Listening to dns for domain proxy.mydomain.com
   Detaching from terminal...
   ```

   This opens up `proxy.mydomain.com` to incoming requests from `iodine`.
5. Open up a terminal on your local box and run:

   ```
   sudo iodine -r proxy.mydomain.com
   ```

   You'll get first a root password prompt and then the `iodine` password
   prompt.

Now you'll have a fully functioning device willing to tunnel all your traffic
via DNS. The IP you specified in step 4 is forwarded onto your local box, so
accessing `10.0.1.1` should bring up the same page as accessing `mydomain.com`.
In order to fully use this and tunnel your traffic, you can use this plain (not
recommended, as it's totally insecure), or create a secondary tunnel through it
(which I did).

All you need to do is set up a SOCKS proxy through the host `10.0.1.1`, which
will then forward your traffic through DNS requests to your server. Neat, huh?

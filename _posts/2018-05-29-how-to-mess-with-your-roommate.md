---
title: "How to mess with your roommate"
layout: post
date: 2018-05-29 15:00:00 PDT
---

Before I explain what I did to hurt poor Logan, I must explain our apartment's
media setup. You will see why shortly.

Logan, if you're reading this, I hope you're more entertained than not.

### The media setup

We have a computer running Ubuntu desktop connected to a TV. This acts as our
media server. Because it is also generally useful for it to be
internet-connected, it also serves a couple of webpages, an SSH server, and
handful of other services.

Since the TV is 4K and the desktop computer is salvaged, the graphics card it
had was not acceptable. Logan decided to purchase a several-generations-old
NVIDIA graphics card (which was far better than our existing one) to display
smooth 4K video.

### The prank

Shortly after the installation, we had a couple of driver glitches, at which
point I thought it would be humorous if there was a way to manually launch
error messages.

After some internet searching, it became apparent that remotely displaying a
message on the TV is as simple as:

1. SSH into the box as the user displaying the content
2. `DISPLAY=:0 zenity --info --text 'Hello!'`

(The `DISPLAY=:0` bit is necessary because there is no display on the session
I am connected to, and I want to show it on the primary viewing display.)

Since we were having trouble with the NVIDIA graphics card, however, I went
with something more similar to:


`DISPLAY=:0 zenity --warning --text 'Display is running in low-graphics mode.'`

I gave this a go (and it worked), but manually logging into the server from an
SSH client _every time_ I wanted to bother Logan was a pain. So I decided to
get crafty.

I thought about making a cron job to bother him regularly, but that has two
problems:

1. It's just that &mdash; regular
2. We have other _real_ cron jobs, which makes the sneaky new one too
   discoverable

Other options like a SysVInit script were also out for similar reasons.

So I decided on instead making a publicly-accessible webpage that has a button
on it: "Fuck with Logan".

### The prank setup

In order to execute, I figured I needed a couple things:

1. Something that can handle user input
2. Something that can execute arbitrary commands on the web user's behalf

Which ends up being:

1. NGINX
2. The NGINX FPM extension
3. PHP
4. The PHP FPM package

So I went and set up a website at [http://our.apartment.server](#) with a
landing page (`logan.html`) and an "action page" (`zenity.php`):

```html
<!-- logan.html -->
<html>
    <head>
        <style type="text/css">
            form button {
                font-size: 20px;
            }

            div.explanation {
                width: 400px;
            }
        </style>

        <meta name="viewport"
              content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
        <meta http-equiv="X-UA-Compatible" content="IE=edge,chrome=1">
        <meta name="HandheldFriendly" content="true">
    </head>
    <body>
        <form method="POST" action="/zenity.php">
            <button>Fuck with Logan</button>
        </form>
    </body>
</html>
```

There are some nonsense `meta` tags in there to make the page easier to use on
phones (I was making this easier to use on-the-go, remember?).

For those who can't render HTML in their heads (most people, I imagine), that
page looks like this:

<div style="border: 1px solid black;">
  <img src="/assets/img/prank/button.png" alt="The button" />
</div>

When the button is clicked, it POSTs to another page that does the heavy
lifting:

```php
<?php
/* zenity.php */

$messages = Array(
    "An error has occurred.",
    "An error has occurred.",
    "An error has occurred.",
    "An error has occurred.",
    "An error has occurred.",
    "An error has occurred.",
    "This graphics driver could not find compatible graphics hardware.",
    "Display driver stopped responding.",
    "Display driver stopped responding.",
    "Display driver stopped responding.",
    "Display driver stopped responding and has recovered.",
    "The system is running in low-graphics mode.",
    "The system is running in low-graphics mode.",
    "The system is running in low-graphics mode.",
    "The system is running in low-graphics mode.",
    "The system is running in low-graphics mode.",
    "The system is running in low-graphics mode.",
    "NVIDIA driver installer stopped responding.",
    "NVIDIA has stopped this device because it has reported problems. (Code 43)",
    "An error has occurred with iface wlx10bef54d395c."
);

$statuses = Array("error", "warning");

$msg = $messages[array_rand($messages)];
$status = $statuses[array_rand($statuses)];
$timeout = "--timeout 10";

exec("sudo -u thedisplayuser /usr/sbin/zenity --$status --display=:0 --text 'Error: $msg' $timeout > /dev/null &");

include 'logan.html';

?>

<div class="explanation">
A pop-up with a randomly-generated error message just appeared on the home
TV and bothered the shit out of Logan, who knows nothing of this website. He is
now wondering why the heck we have so many graphics card / internet / other
issues.
</div>
<br />
<img src='/logan.jpg' />
```

This page does a number of things:

1. Pick a semi-random error message to display
2. Pick the type of dialog box that will appear
3. Display the box for a certain period of time (10 seconds)
4. Render the button and also an explanation of what just happened -- all
   accompanied by a fun picture of Logan himself

For those who can't render HTML in their heads (again, hopefully most people),
that page looks like this:

<div style="border: 1px solid black;">
  <img src="/assets/img/prank/post-click.png" alt="The button" />
</div>

If you're wondering why the webpage allows code execution in such a fashion,
and how the web server owner (`www-data`) could possibly execute commands as
the display user (`thedisplayuser`), you will probably be happy to know that I
strictly limit it in the sudoers file:

```
# /etc/sudoers
www-data ALL=(thedisplayuser) NOPASSWD: /usr/bin/zenity
```

This particular bit of configuration allows for `www-data` to execute only
`/usr/bin/zenity` as `thedisplayuser`, and without a password.

I deployed it after squashing some annoying PHP and NGINX configuration
nonsense. Then, I sent the URL to a couple of friends on campus who know Logan.

### The prank result

Thank goodness Logan reacted as strongly as he did. If he'd been mildly
irritated, I would have been peeved that all my efforts were for nothing. But
no! He lost his cool.

I cannot count how many reboots, driver re-installations, kernel modifications
there were. I only wish I had filmed how angry he got when, after opening VLC,
someone in the apartment popped up a bunch of windows with graphics card
errors.

But I got too amused, and Chris, another roommate, decided to intervene...

### The pranker pranked

#### Step 1

I noticed one day when Logan was asleep that an error message that said "Max is
behind all this" showed up. What??? I had been reverse-pranked! So I
investigated, and found that somebody (Chris), had edited that message into
`zenity.php` to be chosen at random. I promptly removed it (Logan couldn't
figure out the prank just yet), and assumed the fun was over. BUT NO.

#### Step 2

A week or so later, the messaged popped up again. I assumed Chris had noticed
and decided to re-add it to the list. Nope.  Wasn't there. After carefully
checking the file, I noticed that it was now calling `/usr/sbin/zenity` instead
of `/usr/bin/zenity` (the system default) and there was an accompanying entry
in the sudoers file to allow it. What was `/usr/sbin/zenity`? A shell script:

```bash
#!/bin/bash

echo '.' >> /tmp/log.txt
if [ 0 -eq $((RANDOM % 100)) ];
then /usr/bin/zenity --error --display=:0 --text "Max is responsible for these." --timeout 10 > /dev/null &
else /usr/bin/zenity "$@"
fi
```

Well, that's some next-level shit if I've ever seen it. Ninety nine percent of
the time, it does the right thing &mdash; and the other one percent it displays
"Max is responsible for these." I deleted the file (a mistake, I now know) and
sudoers entry, and changed `zenity.php` to the way it was. The messages stopped
appearing. BUT THEN THEY CAME BACK.

#### Step 3

I checked `zenity.php`. Nothing new. `/usr/sbin/zenity`? Gone. Color me
confused.

So I decided I'd peek into `/usr/bin/zenity`:

```bash
#!/bin/bash
# --- SOME BINARY JUNK ---
# --- SOME BINARY JUNK ---
# --- SOME BINARY JUNK ---
# --- SOME BINARY JUNK ---
# --- SOME BINARY JUNK ---
if [ 0 -eq $((RANDOM % 70)) ];
then /usr/bin/rpmdb-client --error --display=:0 --text "M""a""x"" ""i""s r""e""s""p""o""n""s""i""b""l""e"" f""o""r ""t""h""e""s""e." --timeout 10 > /dev/null &
else /usr/bin/rpmdb-client "$@"
fi
```

The crafty little fucker. He'd edited the `zenity` binary directly to BE A BASH
SCRIPT and work 1/70th of the time. What the _hell_. And what even is
`rpmdb-client`?? So I fought back. I modified it:

```bash
# <SNIP>
if [ 0 -eq $((RANDOM % 70)) ];
then /usr/sbin/rpmdb-client --error --display=:0 --text "M""a""x"" ""i""s r""e""s""p""o""n""s""i""b""l""e"" f""o""r ""t""h""e""s""e." --timeout 10 > /dev/null &
else /usr/bin/rpmdb-client "$@"
fi
```

See the difference? The first one calls `/usr/sbin/rpmdb-client` instead of
`/usr/bin/rpmdb-client` and made the `sbin` variant a no-op bash script. With
all luck, he wouldn't notice the one-character change and his message would
never display.

TODO: Figure out the difference between the ELF executable in
`/usr/sbin/zenity` and `/usr/bin/rpmdb-client`, which was created by Chris.
There is some strange binary difference I don't understand yet.

#### Step 4

I decided to fight back further before Chris found out about the one-letter
differential described above. I reverted all of my changes and decided to make
a patch to `zenity` instead. Major props to [Tom Hebb][tchebb] (like in every
other technical post I write) for helping me with this. Here's what I did:

[tchebb]: https://tchebb.me/

1. Configure `apt` to download source packages (in this case, adding a
   `deb-src` line to `/etc/apt/sources.list`)
2. `apt-get source zenity`
3. Make a patch using `quilt`:
    1. `quilt new myPatch.diff`
    2. Patch `src/msg.c` to detect if the word "max" or "Max" is used in the
       message text:
   
       ```diff
       Index: zenity-3.18.1.1/src/msg.c
       ===================================================================
       --- zenity-3.18.1.1.orig/src/msg.c
       +++ zenity-3.18.1.1/src/msg.c
       @@ -21,6 +21,8 @@
         * Authors: Glynn Foster <glynn.foster@sun.com>
         */
        
       +#include <string.h>
       +
        #include "config.h"
        
        #include "zenity.h"
       @@ -85,6 +87,11 @@ zenity_msg (ZenityData *data, ZenityMsgD
          GObject *text;
          GObject *image;
        
       +  if (strstr(msg_data->dialog_text, "Max")
       +      || strstr(msg_data->dialog_text, "max")) {
       +      return;
       +  }
       +
          switch (msg_data->mode) {
            case ZENITY_MSG_WARNING:
              builder = zenity_util_load_ui_file ("zenity_warning_dialog", NULL);
       ```
   
    3. `quilt add src/msg.c`
    4. `quilt pop`
4. `dpkg-source --commit`
5. `dpkg-buildpackage -us -uc`
6. Realize that creating and installing a new package is more noticeable
   than just silently replacing the binary
7. Silently replace the `rpmdb-client` (his `zenity`) binary with my compiled
   version of `zenity`
8. Pat myself on the back

This patch changes `zenity` so that if it notices the message contains the text
"max" or "Max", it quietly does nothing.

UPDATE: So far (end of March 2018), Chris has not noticed that I have modified
the binary. Logan has also therefore not noticed any messages that include my
name.  There is as of yet no end in sight to the prank, unless I decide to tell
him when we graduate.

UPDATE: We graduated and all parted ways. Though Logan and I will be living
together again next year, we will likely not be in the house enough to carry on
this prank. I decided to publish this post before attempting further
shenanigans.

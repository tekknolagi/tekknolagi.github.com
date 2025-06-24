---
title: "Heating water from afar"
layout: post
---

<b>Please do not take anything you read in any of my posts (but
<i>especially</i> not this post) as engineering advice.</b>

My parents' house has an on-demand water heater for energy efficiency reasons.
This has a small drawback: you have to press a button to prime the water heater
and then wait 2-5 minutes before showering.

This turns into a somewhat bigger drawback for the one room for which it's just
Not Possible to wire up a button. The water heater company hypothetically sells
a plug-and-play wireless solution for this sort of thing, but that is seemingly
incompatible with my parents' walls (???).

Thankfully, I have a [have a playbook](/blog/wakeonlan/) for this.

There's just one problem: how do I make a Raspberry Pi talk to the water
heater? I investigated a couple of different approaches:

* analyze and replicate the water heater company's proprietary Bluetooth
  protocol
* install a physical button presser

but after a couple of discussions with my dad and a support tech from the
company, we determined that we should instead *emulate* a button press. To find
out what that means, let's take a look at a very sketchy wiring diagram:

<figure>
<object type="image/svg+xml" data="/assets/img/heater-button.svg"></object>
<figcaption>The heater supplies 12V at 0.2A on the red wire. When the button is
pressed, it connects the red and black wires, which the heater interprets as a
signal to start heating. The wire nuts are there because several buttons are
already joined together this way.</figcaption>
</figure>

This means we would have to add a new "button" and have it briefly connect two
wires. Because the last time I actually touched some wires was over 10 years
ago in robotics and I don't want to start any fires, I reach out to the usual
suspects: Tom and Logan. They inform me that the thing I am looking for is
called a relay and that companies sell pre-built relay hats for the Pi. Super.

I ended up buying:

* A Pi Zero 2W and accoutrements
* A relay hat with headers and stabilization posts
* Telephone wire from the hardware store, which already has bundled red/black
  wire

At some point I sit down to build the thing and realize that I don't actually
know how relays work. The relay I bought had three ports: NC, NO, COM. After
some searching, I figure out that I want one wire in NO ("normally open") and
one in COM ("common"). This means that the relay, when activated, will close
the circuit.

<figure>
<object type="image/svg+xml" data="/assets/img/heater-button-pi.svg"></object>
</figure>

I downloaded the sample code from the company that sells the relay hats and
realized that it is an extremely thin (~10 LOC) wrapper over the existing
Python GPIO library provided and pre-installed by Raspberry Pi, so I just
manually inlined it:

```python
#!/usr/bin/env python3
import RPi.GPIO as GPIO
import time

GPIO.setmode(GPIO.BOARD)

class relay:
    relay_pins = {"R1":31,"R2":33,"R3":35,"R4":37}

    def __init__(self, pins):
        self.pin = self.relay_pins[pins]
        self.pins = pins
        GPIO.setup(self.pin,GPIO.OUT)
        GPIO.output(self.pin, GPIO.LOW)

    def on(self):
        GPIO.output(self.pin,GPIO.HIGH)

    def off(self):
        GPIO.output(self.pin,GPIO.LOW)

print("Starting water heater...")
r2 = relay("R2")
# time is in seconds, so turn on once for 4ms and then turn off again
TIME_IN_MS=4
MS_PER_SEC=1000
r2.on()
time.sleep(TIME_IN_MS/MS_PER_SEC)
r2.off()
print("...signal sent. Please wait 5 minutes before showering.")
```

If you read my previous post (linked above), you will know that is is, of
course, a CGI script that is triggered on a website button press:

<figure>
<img src="/assets/img/heater-website.png" />
<figcaption>The tagline under #justshowerthings is randomly selected on page
load from a medium-long list I wrote. All the items are carefully designed to
toe the line.</figcaption>
</figure>

All of the rest of the software is the same as in the previous post. Very
boring stuff: httpd, systemd. Hopefully nothing goes wrong. But if it does and
I need to administer this device from afar, I also set up Tailscale (no, this
is not an ad; just happy).

The total bill for this came to ~$40 or so, which isn't half bad. It could
probably be done for 35 cents using an old microcontroller and a paperclip or
something but I wanted an exceptionally boring (to me) approach. That's all for
now. Thanks for reading!

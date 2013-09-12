---
layout: post
published: false
title: Using a Raspberry Pi as a mail server
---

Software:
* Postfix
  1. EHLO response
  2. SPF record
  3. DKIM
  4. send domain
* Courier
* Dovecot

Pi:
* SSH server run by default
* SD as boot, external as main ext4 disk
* tunnel for remote ssh

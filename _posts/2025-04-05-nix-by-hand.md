---
title: Nix derivations by hand, without guessing
layout: post
---

I went on a little low-level Nix adventure yesterday and early this morning
because of [this excellent blog post][farid]. In it, Farid builds up the
simplest possible Nix derivation---making a file that has the contents "hello
world". Here's what I like about it:

* No Nix language; just the really fiddly low-level bits of building a
  derivation by hand
* It's incremental, building from a little friendly JSON blob and incrementally
  adding scarier parts
* It only uses two low-level Nix commands

[farid]: https://fzakaria.com/2025/03/23/nix-derivations-by-hand

One thing that I did not understand after reading, though, was where the hashes
come from. Farid's post does the same thing that I occasionally do with the C++
or Rust compiler, where we intionally cause an error to get information that
the compiler already knows. That's not very satisfying for someone (me) who is
starting basically from zero. So I went down a bit of a rabbit hole trying to
figure out how to manually generate these hashes.

But first, let's get some terms out of the way. Please forgive (but help me
correct) mistakes along the way, because this is my first time messing around
with Nix.

## What on earth is a derivation?

As far as I can tell, it's a very precise recipe for building a file. It's kind
of like a Make recipe or a shell script except all of its inputs *and outputs*
refer to well-known long paths in `/nix/store`.

Here's an example from Farid's blog that we will be trying to replicate in this
post that has one of these path names:

```json
{
  "name": "simple",
  "system": "x86_64-linux",
  "builder": "/bin/sh",
  "outputs": {
    "out": {
      "path": "/nix/store/5bkcqwq3qb6dxshcj44hr1jrf8k7qhxb-simple"
    }
  },
  "inputSrcs": [],
  "inputDrvs": {},
  "env": {
    "out": "/nix/store/5bkcqwq3qb6dxshcj44hr1jrf8k7qhxb-simple"
  },
  "args": [
    "-c",
    "echo 'hello world' > $out"
  ]
}
```

In it, we have a `name` field (your pick) and a `system` field (I am guessing
there are a couple of well-known system/platform names. I am also running
x86\_64 Linux by happenstance. If you follow along, your hashes will be the
same as my hashes *only if* you are on the same system, I think.).

We also have this thing called a `builder`, which is the singular command that
gets run. In this case, we also pass it `args` (at the bottom) and environment
variables from `env`. Don't think too hard about `inputSrcs` and `inputDrvs`
because they don't come into play in this post.

Last, we have `outputs`, which, like `env`, also has this magical path name.
It's the output file (or directory, I think) that your Nix derivation is
required to create. The name `out` in each of `outputs` and `env` is arbitrary
and I think they don't have to have the same name. Just convention.

This derivation roughly corresponds to the following Make recipe (remember that
Make variables also start with `$` so you have to escape your shell variables
like `$${var}`):

```make
/nix/store/5bkcqwq3qb6dxshcj44hr1jrf8k7qhxb-simple:
	export out=/nix/store/5bkcqwq3qb6dxshcj44hr1jrf8k7qhxb-simple; \
    echo 'hello world' > $${out}
```

But where does that huge path come from?

## Getting set up

I didn't want to install Nix, so I did all of my explorations inside Docker. If
you're not going to use Nix long-term, I recommend you do the same, since it
otherwise makes some pretty invasive system changes.

Here's all you need to get started:

```docker
FROM nixos/nix AS builder
```

Now you can build a Docker container:

```console
$ docker build . -t notnix
...
$
```

and then start `/bin/sh`, the only shell in an easy-to-remember path:

```console
$ docker run -i -t notnix /bin/sh
sh-5.2#
...
```

Any time I use a Nix command in the rest of the article, it's either in a
Docker `RUN` command or in `/bin/sh` inside a running Nix container.

Now let's find some hashes.

## Finding hashes

John Ott pointed out to me that Nix [docs on store paths][store-paths] had a
partial answer. He said that the path comes from hashing the ATerm
representation of the derivation with the outPath set to an empty string.

[store-paths]: https://nixos.org/guides/nix-pills/18-nix-store-paths.html

...what?

So after some digging and re-reading Farid's post, apparently ATerm is an
old(er) configuration language that looks kind of like building OCaml variants.
And I guess it makes sense that we shouldn't need the path to calculate the
path (otherwise we'd be in circular trouble). So I had Nix create the ATerm
form of my JSON derivation without any paths:

```json
{
  "name": "simple",
  "system": "x86_64-linux",
  "builder": "/bin/sh",
  "outputs": {
    "out": {
    }
  },
  "inputSrcs": [],
  "inputDrvs": {},
  "env": {
  },
  "args": [
    "-c",
    "echo 'hello world' > $out"
  ]
}
```

by running:

```console
$ nix --extra-experimental-features nix-command derivation add < simple.json
/nix/store/1p6dixyqvjddfq5fmys3i55nl90ckjam-simple.drv
$
```

Running that command outputs the path of the ATerm form file, which we can
check out:

```console
$ cat /nix/store/1p6dixyqvjddfq5fmys3i55nl90ckjam-simple.drv
Derive([("out","","","")],[],[],"x86_64-linux","/bin/sh",["-c","echo 'hello world' > $out"],[("out","")])$
```

You can see they are making sure to remove all whitespace, even the trailing
newline (hence the `$` at the end, which is my shell prompt).

Okay, so we have an ATerm form of the derivation and it has no output path. I
guess we hash it? I got a little lost at this point until Jamey Sharp chimed in
with the even more detailed [store path specification][store-path-spec].

[store-path-spec]: https://nix.dev/manual/nix/2.24/protocols/store-path

This clarified, after many reads, that we have to do the following steps. At
some point I switched to using Python because it got a little text manipulation
heavy:

1. Call the ATerm derivation sans output path the `inner-fingerprint` because
   we're not doing anything with `text` or `source` types or NARs or something
1. Make a SHA256-hash of the `inner-fingerprint` and then base16-encode it.
   That's called the `inner-digest`

Okay, not so bad:

```python
import hashlib
import base64

with open(inner_fingerprint, "rb") as f:
    inner_fingerprint_hash = hashlib.file_digest(f, "sha256").digest()
inner_digest = (
    base64.b16encode(inner_fingerprint_hash).decode("utf-8").lower()
)
```

Then, once we have that, we do some more stuff to it:

1. Combine the `inner-digest` with some other fields like the derivation's
   `name` and call that the `fingerprint`
1. Hash that, take the first 20 bits, and take the base32 representation of
   that

Alright, not so bad, Python can do all of this in the standard library:

```python
name = deriv["name"]  # "simple"
# the "out" is the name we picked earlier and is arbitrary
fingerprint = f"output:out:sha256:{inner_digest}:/nix/store:{name}"
fingerprint_hash = hashlib.sha256(fingerprint.encode("utf-8")).digest()
fingerprint_digest = hashlib.b32encode(fingerprint_hash[:20])
```

*However.* The docs are misleading about two things and that sent me on a merry
chase.

First of all, Nix does *not* use normal base32. They use a different character
set. Also, they base32 *in reverse*. I didn't figure either of these things out
until *tombl* chimed in on Twitter.

Second of all, the store-path docs are outright lying when they say "the first
160 bits [20 bytes] of a SHA-256 hash". Instead, what they *should say* is "do
this weird XOR thing on the hash, folding it back onto itself kinda."

I only got that second bit by digging through [the Nix C++
codebase][nix-compresshash]. So instead, what we really want is this:

[nix-compresshash]: https://github.com/NixOS/nix/blob/cf5e59911bb47e4d64a57270429a70f380076c1c/src/libutil/hash.cc#L387

```python
def to_nix_base32(bytes_data):
    b32_alphabet = b"ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"
    b32_nix = b"0123456789abcdfghijklmnpqrsvwxyz"
    trans = bytes.maketrans(b32_alphabet, b32_nix)
    return base64.b32encode(bytes_data[::-1]).translate(trans).decode("utf-8")

def compress_hash(h, newlen):
    result = bytearray(b"\0" * newlen)
    for i in range(len(h)):
        result[i % newlen] ^= h[i]
    return bytes(result[:newlen])

fingerprint = (
    f"output:{output}:sha256:{inner_digest}:{STORE_DIR}:{name}"
)
fingerprint_hash = hashlib.sha256(fingerprint.encode("utf-8")).digest()
fingerprint_digest = to_nix_base32(compress_hash(fingerprint_hash, 20))
```

I'm using `bytes.maketrans` because Python makes it easy enough to convert
between normal-base32 and nix-base32 but you should also take a look a
standalone implementation of nix-base32. For example, here is [a link to
Tvix's][tvix-base32].

[tvix-base32]: https://github.com/tvlfyi/tvix/blob/ac27df9ae51f69c1b746b7c8c2ad22f5a857ca52/nix-compat/src/nixbase32.rs#L17

This magic number that pops out of the correct fingerprint hashing method is
the same as the one from Farid's post! `5bkcqwq3qb6dxshcj44hr1jrf8k7qhxb`

Now we can add that back into the JSON as an output path and an environment
variable to finally get the same JSON blob as before:

```json
{
  "name": "simple",
  "system": "x86_64-linux",
  "builder": "/bin/sh",
  "outputs": {
    "out": {
      "path": "/nix/store/5bkcqwq3qb6dxshcj44hr1jrf8k7qhxb-simple"
    }
  },
  "inputSrcs": [],
  "inputDrvs": {},
  "env": {
    "out": "/nix/store/5bkcqwq3qb6dxshcj44hr1jrf8k7qhxb-simple"
  },
  "args": [
    "-c",
    "echo 'hello world' > $out"
  ]
}
```

And, just as Farid promised, Nix accepts our derivation JSON and gives us a new
ATerm:

```console
$ nix --extra-experimental-features nix-command derivation add < simple.json
/nix/store/vh5zww1mqbcshfcblrw3y92v7kkzamfx-simple.drv
$
```

It's same derivation Path that Farid has in his post, too.

But having a derivation in hand doesn't mean anything other than we
have---finally---written a correct recipe to build a thing. Let's run it and
see the output!

```console
$ nix-store --realize /nix/store/vh5zww1mqbcshfcblrw3y92v7kkzamfx-simple.drv
...
/nix/store/5bkcqwq3qb6dxshcj44hr1jrf8k7qhxb-simple
$ cat /nix/store/5bkcqwq3qb6dxshcj44hr1jrf8k7qhxb-simple
hello world
$
```

I'm calling that success. We build a derivation by hand without any
guess-and-check!

Check out [my Python code][pix] if you like.

[pix]: https://github.com/tekknolagi/manual-nix/tree/815b670eee196447ad52a6bce25068575ff2bd65

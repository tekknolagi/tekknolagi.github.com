---
title: "scrapscript.py"
layout: post
date: 2024-01-23
---

[Scrapscript](https://scrapscript.org/) is a small, pure, functional,
content-addressable, network-first programming language. It's designed to allow
creating small, simply shareable programs. The language was created by
[Taylor Troesh](https://taylor.town/) and the main implementation was created
by me and [Chris](https://www.chrisgregory.me/).

```
fact 5
. fact =
  | 0 -> 1
  | n -> n * fact (n - 1)
```

I'm not going to fill out the [usual
checklist](https://www.mcmillen.dev/language_checklist.html)---that's not the
point of the post, and a lot of it is answered on the website. This post is
more about the implementation.

## A bit of history

In April of 2023, I saw scrapscript posted on Hacker News and sent it to Chris.
We send each other new programming languages and he's very into functional
programming, so I figured he would enjoy it. He did!

But we didn't see any links to download or browse an implementation, so we were
a little bummed. We love trying stuff out and getting a feel for how it works.
A month or two passed and there still was not an implementation, so we decided
to email Taylor and ask if we could help.

> Subject: Chris and I would like to help you with scrapscript
>
> Hi Taylor,
>
> My friend Chris (cc'ed) and I are excited about scrapscript. This is the kind
> of project we have talked about building for years. He's an ML guy with an
> unreasonable interest in Haskell and I'm a PL/compilers guy (not as much into
> Haskell) and would be very interested in chipping in, or at least trying
> early betas.
>
> Chris: https://www.chrisgregory.me/\
> Me: https://bernsteinbear.com/
>
> Cheers,\
> Max

Taylor was very gracious about the whole thing and shared his [small JavaScript
implementation of
scrapscript](https://github.com/tekknolagi/scrapscript/blob/71d1afecc32879aed9c80a3ed17cb81fe1c010d6/scrapscript.ts).
For such a little file, it implemented an impressive featureset.

Chris nor I are particularly good at JavaScript and we figured more
implementations couldn't hurt, so we decided to make a parallel implementation
with the same features. While Taylor's primary design constraint was
implementation size, ours was some combination of readability and correctness.
This meant that we wrote a lot of tests along the way to pin down the expected
behavior.

Two days into this little hackathon, we told Taylor and he was pretty happy
about it, so we started having semi-regular chats and continued hacking. Three
weeks later, we opened up [the repo](https://github.com/tekknolagi/scrapscript)
to the public. Please take a look around! Most of it is in one file,
`scrapscript.py`. This was one of our early design decisions...

## Implementation design decisions

While we weren't explicitly trying to keep the implementation size down like
Taylor was, we did want to keep it self-contained. That led to a couple of
implicit and explicit design choices:

**No external dependencies for core features.** Keep the core understandable
without needing to go off and refer to some other library.

As a sort of corollary, **try to limit dependencies on unusual or fancy
features of the host programming language (Python).** We wanted to make sure
that porting the implementation to another programming language was as easy as
possible.

**Test features at the implementation level and end-to-end.** Write functions
that call `tokenize` and `parse` and `eval_exp` explicitly so that we catch
errors and other behavior as close to the implementation as possible. Also
write full integration tests, because those can be used as a feature showcase
and are easily portable to other implementations.

```python
class Tests(unittest.TestCase):
    def test_tokenize_binary_sub_no_spaces(self) -> None:
        self.assertEqual(tokenize("1-2"), [IntLit(1), Operator("-"), IntLit(2)])
    # ...
    def test_parse_binary_sub_returns_binop(self) -> None:
        self.assertEqual(
            parse([IntLit(1), Operator("-"), IntLit(2)]),
            Binop(BinopKind.SUB, Int(1), Int(2)),
        )
    # ...
    def test_eval_with_binop_sub(self) -> None:
        exp = Binop(BinopKind.SUB, Int(1), Int(2))
        self.assertEqual(eval_exp({}, exp), Int(-1))
    # ...
    def test_int_sub_returns_int(self) -> None:
        self.assertEqual(self._run("1 - 2"), Int(-1))
```

Choosing Python was mostly a quirk. Python is not in general special; it just
happens to be a language that Chris and I have worked with a lot.

### Consequences of our testing strategy

Making sure to test early and test thoroughly had some excellent consequences.
It meant that we could keep a record of the expected behavior we discovered in
`scrapscript.js` as we ported it and have that record continuously checked. It
also meant that as we [gutted and re-built
it](https://github.com/tekknolagi/scrapscript/commit/082e30375225394f30fd270ffdcee7f5d63173ae),
we felt very confident that we weren't breaking everything.

```diff
 def tokenize(x: str) -> list[str]:
-    # TODO: Make this a proper tokenizer that handles strings with blankspace.
-    stripped = re.sub(r" *--[^\n]*", "", x).strip()
-    return re.split(r"[\s\n]+", stripped)
+    lexer = Lexer(x)
+    tokens = []
+    while lexer.has_input():
+        tokens.append(lexer.read_one())
+    return tokens
```

All the tests continued to pass and we could even enable a new one!

## Why is this interpreter different from all other interpreters?

It's not. It's a pretty bog-standard tree-walking interpreter for a juiced-up
lambda calculus[^church-encodings]. Perhaps we'll eventually generate bytecode
or some other IR and compile it, but we do not have any performance problems
(yet). And scrapscript doesn't feel like an "industrial strength" language;
nobody is writing large applications in it and the language is expressly not
designed for that.

[^church-encodings]: Chris even built a [little
    demo](https://github.com/gregorybchris/scraps/blob/6d8583e8a7df504b1a855687231f279a35b6de83/church.scrap)
    of Church encodings for numbers.

It's a little different *for me*, though, because it has features that I have
never implemented before! In particular, scrapscript supports some pretty
extensive pattern matching, which we had to learn how to implement from
scratch.

```python
def eval_exp(env: Env, exp: Object) -> Object:
    # ...
    if isinstance(exp, Apply):
        callee = eval_exp(env, exp.func)
        arg = eval_exp(env, exp.arg)
        # ...
        if isinstance(callee.func, MatchFunction):
            for case in callee.func.cases:
                m = match(arg, case.pattern)
                if m is None:
                    continue
                return eval_exp({**callee.env, **m}, case.body)
            raise MatchError("no matching cases")
        # ...
```

It's also different because it's the first from-scratch language implementation
I have worked on with someone else (I think). Chris has been an excellent
co-implementor, which is very impressive considering it his first programming
language implementation *ever*!

## Some neat implementation features

Why have a little programming project if you don't get to try out some new
tricks and techniques?

### The REPL

You might have seen [my recent blog post](/blog/simple-python-repl/) about
building a featureful REPL using a nice library that Python gives you. I wrote
that post while learning about all that stuff for scrapscript. Scrapscript's
REPL implementation is pretty short but it has `readline` support, tab
completion, line continuation, and more. Thanks, Python!

```
>>> $$[^tab]
$$add         $$fetch       $$jsondecode  $$listlength  $$serialize
>>> $$add
```

### An actually portable executable

We build scrapscript as an Actually Portable Executable using
[Cosmopolitan](https://justine.lol/cosmopolitan/) by Justine Tunney. This means
that it is packaged with a small libc implementation and Python runtime into
one (reasonably) small, self-contained executable. This executable is
theoretically runnable on all major platforms without fuss. And the Docker
container that we build with it is ~~36MB~~ 25.5MB in total (!) because it does not need
to have a bunch of operating system stuff in the filesystem.

```
$ docker images ghcr.io/tekknolagi/scrapscript
REPOSITORY                      TAG    IMAGE ID       CREATED       SIZE
ghcr.io/tekknolagi/scrapscript  trunk  16867189d853   3 hours ago   25.5MB
$
```

Check out
[build-com](https://github.com/tekknolagi/scrapscript/blob/e38210f7aa8ce375a7e615b301922bd7b9710d37/build-com)
and [the
Dockerfile](https://github.com/tekknolagi/scrapscript/blob/e38210f7aa8ce375a7e615b301922bd7b9710d37/Dockerfile)
for more information.

### The web REPL

We wanted to have an interactive playground like a bunch of other programming
languages do. I, however, didn't feel like implementing scrapscript a second
time. So instead I wrote a little stateless server program in Python that is a
function `(env, exp) -> (env', result)` and a JS program to drive the web
requests. What you get is [the web REPL](https://scrapscript.fly.dev/repl).
Building this required being able to serialize objects and environments so that
they could be stored on the client as opaque blobs. That's mostly working, but
I don't have a full solution for objects with cycles. Yet. It's in progress!

## In progress features

As I mentioned before, serializing objects with cycles is a work in progress.
This requires adding support for fake `ref` types inside the serializer and
resolving them in the deserializer. It should be ready to ship soon enough,
though.

We also don't have full support for alternates as described on the main
website. It shouldn't be particularly difficult but nobody has implemented it
yet. We did get symbols working, though, so we have `#true` and `#false`
implemented inside scrapscript.

```
some-sum-type :
  #cowboy
  #ron int
  #favcolor (#green #blue #other)
  #friend int
  #stranger text
```

We're also working on the first implementations of scrapyards. We're not sure
exactly what design direction to go yet so Taylor and I have each prototyped it
in different ways. My implementation uses Git as a versioned blob store to
be very lazy about it and re-use a bunch of existing infrastructure. That's one
of my personal goals for this implementation: minimal implementation first.

Scrapscript comes with a notion of platforms---different sets of APIs given to
a scrap by the host running it. It's not really built in yet but I did
prototype a "web platform". This was a little Python shell around a scrap that
fed it web requests as scrap records. Then the scrap could use pattern matching
to route the request and build up a request. As Taylor says on the website,
scrapscript is pretty decent for an HTML DSL.

```
handler
. handler =
  | { path = "/" } -> (status 200 <| page "you're on the index")
  | { path = "/about" } -> (status 200 <| page "you're on the about page")
  | x -> (status 404 <| page "not found")
. status = code -> content -> { code = code, content = content }
. page = body -> "<!doctype html><html><body>" ++ body ++ "</body></html>"
```

In the future I think it could be fun to---similar to the web REPL---write a
function `(db, request) -> (db', response)` and have a stateless webserver
kernel that can still store data if the outside world applies the delta to the
database.

Chris and I also talked about building out a graphics API or graphics platform.
We're not sure what this looks like, but he is into cellular automata and it
would be neat to be able to write pixels directly from scrap without going
through PPM or something.

Last, but certainly not least, we are working on a scrapscript compiler. The
neat thing, though, is that this compiler is written *in scrapscript*. The goal
is to port scrapscript to the browser not by writing a new interpreter in JS,
but by compiling the compiler to JS (on top of the Python interpreter), then
running the compiler (now JS code) on the web.

```
compile =
| {type="Int", value=value} -> $$int_as_str value
| {type="Var", name=name} -> name
| {type="String", value=value} -> $$str_as_str value
| {type="Binop", op="++", left=left, right=right} ->
    -- Special case string concat
    (compile left) ++ "+" ++ (compile right)
| {type="Binop", op=op, left=left, right=right} ->
    (compile left) ++ op ++ (compile right)
| {type="List", items=items} ->
    "[" ++ (join ", " (map compile items)) ++ "]"
-- ...
```

## Thanks for reading

Want to learn more? Well first, play with [the web REPL](https://scrapscript.fly.dev/repl). Then
take a look at [the repo](https://github.com/tekknolagi/scrapscript) and start
contributing! Since we don't have a huge backlog of well-scoped projects just
yet, I recommend posting in the [discourse
group](https://scrapscript.discourse.group/) first to get an idea of what would
be most useful and also interesting to you.

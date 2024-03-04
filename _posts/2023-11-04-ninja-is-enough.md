---
title: Ninja is enough build system
layout: post
description: For small projects, you can DIY Ninja files instead of using CMake
  or Meson!
date: 2023-11-04
---

Sometimes, you have to write software. Sometimes, that software spans multiple
files. And sometimes you just don't want to build that software with `cc *.c`.

Some people write build scripts in shell or some other programming language to
solve their problems. This is a lot of work to get right and get fast because
you end up rewriting a lot of core features (dependency graphs, job servers,
etc) by hand. So the other default choice is Make, which comes with a lot of
features built-in. Nice! But it also comes with some problems that I won't get
into here. Not so nice.

Fortunately, I am here to tell you that there is an interesting alternative for
small projects: Ninja. And you can even write a program to generate Ninja for
you, if you want.

## A look at Ninja

Ninja is meant to be a lower-level version of Make so that program builds,
especially incremental rebuilds, can be as fast as possible. It was developed
for use at Google building the Chrome browser. Its syntax looks like this:

```
# build.ninja
cc = clang
rule cc
  command = $cc $cflags -c -o $out $in
rule ld
  command = $cc -o $out $in $ldflags
build main: ld main.o lib.o
build main.o: cc main.c
build lib.o: cc lib.c | lib.h
```

In this example we have three main constructs: variable declarations (`cc =
clang`), variable references (`$cc`), rule declarations (`rule cc` ...), and
build target declarations (`build main:` ...). We also have some undefined
variables like `cflags` and `ldflags`. They expand to the empty string.

This Ninja file is enough to build a small project consisting of `lib.h`,
`lib.c`, and `main.c`. Ninja will automatically parallelize as much of the
build process as possible by default and produce minimal output when
successful:

```console
$ ninja
[3/3] clang -o main main.o lib.o
$
```

The `[3/3]` bit indicates that the third target of three has been built. The
other output lines got erased, so my terminal is pretty clean.

Speaking of clean, you don't even need to write your own `clean` rule like you
would in a Makefile. Ninja has a built-in tool[^tools] to remove all targets:
`ninja -t clean`.

[^tools]: There are a couple other pretty useful builtin tools like `compdb`,
    which will generate a JSON compilation database that other tools can read
    and `graph`, which will generate a GraphViz representation of your build
    graph.

Altogether, it looks like Ninja *alone* is useful enough to replace small
Makefiles that do not do anything interesting. But what if we want to
dynamically change the values of variables, use different files on different
platforms, or potentially something more complicated? Well, we need to generate
the Ninja file[^migration].

[^migration]: It might be useful to have a tool that translates
    raw/hand-written Ninja files into their equivalent Python programs to make
    the growth of the project a little easier. This means you can start off
    with a little bit of Ninja but as soon as you need to do some dynamic
    things, you have an escape hatch and don't need to re-write everything
    yourself.

## Generating Ninja

There are tools like CMake and Meson that can generate Ninja files for you if
you use their languages. Google even wrote a whole tool called `gn` to generate
Ninja to build Chrome. But their languages leave much to be desired. And why
should you have to learn a whole new programming language just for your build
tool?

For medium-complexity projects, you don't have to. Ninja ships with a file
called `ninja_syntax.py` (~200 lines of code) that you can download and check
in alongside your project. It comes with some helper functions to generate
Ninja files without doing all the string-slinging yourself. For example, we can
replicate the above Ninja file in Python:

```python
import ninja_syntax
import os
import sys


writer = ninja_syntax.Writer(sys.stdout)
writer.variable("cc", os.getenv("CC", "clang"))
writer.rule("cc", "$cc $cflags -c -o $out $in")
writer.rule("ld", "$cc -o $out $in $ldflags")
writer.build("main", "ld", ["main.o", "lib.o"])
writer.build("main.o", "cc", "main.c")
writer.build("lib.o", "cc", "lib.c", implicit=["lib.h"])
```

And when you run it, the output looks identical to what we had hand-written
before:

```console
# gen.py
$ python3 gen.py
cc = clang
rule cc
  command = $cc $cflags -c -o $out $in
rule ld
  command = $cc -o $out $in $ldflags
build main: ld main.o lib.o
build main.o: cc main.c
build lib.o: cc lib.c | lib.h
$
```

You may be wondering what value this added over hand-writing the Ninja. If so,
take a look at the Python again! I did something sneaky: we can now change what
compiler we are using without manually modifying the Ninja file. Take a look:

```console
# gen.py
$ CC=tcc python3 gen.py
cc = tcc
rule cc
  command = $cc $cflags -c -o $out $in
rule ld
  command = $cc -o $out $in $ldflags
build main: ld main.o lib.o
build main.o: cc main.c
build lib.o: cc lib.c | lib.h
$
```

Wow!

You can probably imagine more things to do at Ninja-generation time, like
optionally using ccache or changing flags or using `pathlib.Path.rglob` or
something[^other-ideas].

[^other-ideas]: Or support out-of-tree builds, different platforms, or maybe
    you could even do some kind of easily distributed build using Erlang, or...

**(a small update)**

Andy Chu of [Oils](https://www.oilshell.org/) fame noted that Ninja can use
compiler-generated Make `.d` depfiles natively. This means that we can remove
the manual `implicit=` header dependencies from the individual `.o` rules and
instead generate them using `-MD -MF $out.d` and `depfile=` in the `cc` rule.

```python
# ...
writer.rule("cc", "$cc -MD -MF $out.d $cflags -c -o $out $in", depfile="$out.d")
# ...
writer.build("lib.o", "cc", "lib.c")
```

For compilers such that support this (GCC and Clang both do), this is a very
convenient option. It even adds dependencies on system headers in case those
change.

And, if you use GCC/Clang (and with some tweaking, MSVC), you can also add
`deps="gcc"` or `deps="msvc"` to have Ninja internalize the `.d` file and
delete it after it's been processed. According to the docs, this makes for
faster builds on larger projects.

```python
# ...
writer.rule("cc", "$cc -MD -MF $out.d $cflags -c -o $out $in", depfile="$out.d", deps="gcc")
# ...
```

Enjoy.

**(end update)**

Another question you might have: do you have to manually add new files to this
Ninja-generator and re-generate Ninja manually?

## Regenerating Ninja when we change the Ninja generator

Nope. Systems like CMake and Meson do this automatically and so can we. Add
these two lines to `gen.py`:

```python
# gen.py
# ...
writer.rule("regen_ninja", f"{sys.executable} $in > $out")
writer.build("build.ninja", "regen_ninja", __file__)
```

The first line adds a rule called `regen_ninja` that runs a command and pipes
its output to a file. The second line adds a target called `build.ninja` that
gets triggered whenever the `gen.py` changes. Try it out:

```console
$ ninja
ninja: no work to do.
$ touch gen.py
$ ninja
[1/1] /usr/bin/python3 /path/to/gen.py > build.ninja
ninja: no work to do.
$
```

And it doesn't even rebuild the native targets.

## An idea I haven't had time to work on

At some point I wondered if we could replicate enough of Bazel or Buck syntax
and semantics to be able to build small projects with this kind of hackery. I
don't mean full hermeticity build farms or anything (although you might be able
to get away with some tricks like [Landlock Make](https://justine.lol/make/)
does). That all sounds complicated[^how-to]. I just mean having parallel
builds, nice output, and a small library of functions like `cc_binary`.

[^how-to]: Andreas Zwinkau seems to think this might not actually be that
    difficult and one could theoretically re-use the wrapper program that Bazel
    uses. See [his post](https://beza1e1.tuxen.de/hermetic_builds.html) for
    more information. Or maybe we could use Nix like
    [Nix-Bazel](https://nix-bazel.build/).

If you wanted the same kinds of guarantees about meta build system termination
that Bazel and Buck provide, you probably can't use Python anymore. They don't
use Python; they use a similar language called Starlark that is a *total*
language. It's impossible to write a program that runs forever in Starlark.
That's a cool property to have.

I think the various Starlark implementations such as
[starlark-rust](https://github.com/facebookexperimental/starlark-rust/) are
somewhat usable as libraries, so it may be possible to add some built-in
functions to Starlark for generating Ninja, some wrapper functions like
`cc_binary`, and then bundle that together as `bazel-lite`.

## How is this different from CMake/Meson/autotools?

It's much *much* smaller. And you could use any meta-build programming language
you like if you write the Ninja syntax file. And now hopefully you know a
little more about what is going on under the hood.

For comparison, let's take a look at a `CMakeLists.txt` for the same
hypothetical C project:

```cmake
# CMakeLists.txt
project(demo)

add_executable(main main.c lib.c)
```

It's pretty terse, but does a lot. There is a lot of hidden behavior:

* Finding dependencies on `lib.h` by using the compiler
* Making intermediate `.o` files instead of compiling all the `.c`
* Supporting different compilers with `CMAKE_C_COMPILER`
* Supporting different optimization levels with `CMAKE_BUILD_TYPE`
* and more...

That's great stuff, sure. It's all kind of hidden behavior that you need to
discover over time. And you need to install CMake.

You may want to use CMake for your project. You may not. Both are fine.

## Why I wrote this post

I was looking at Cliff Click's new [expository
project](https://github.com/SeaOfNodes/Simple) to teach the world about Sea of
Nodes. I didn't know how to build it without running `javac $(find src/main
-type f -name '*.java')`, which just did not seem like a good solution. So I
wrote a little Ninja generator in about 40 lines of code. Then I thought I
would write about it.

## Further reading

Check out Julia Evans' [excellent
post](https://jvns.ca/blog/2020/10/26/ninja--a-simple-way-to-do-builds/) about
Ninja. This post is accidentally very similar for the first two thirds.

### The various Ninja implementations

* [Ninja](https://github.com/ninja-build/ninja)
* [Samurai](https://github.com/michaelforney/samurai)
* [n2](https://github.com/evmar/n2)
* [Turtle](https://github.com/raviqqe/turtle-build)
  * I knew about this before adding it to the post but only when adding a link
    to it did I understand that it might be a joke about Teenage Mutant Ninja
    Turtles.
* [ninja-rs](https://github.com/nikhilm/ninja-rs)
* This [tiny little implementation in Python](https://github.com/gkbrk/scripts/blob/master/ninja.py)!
* [Siso](https://chromium.googlesource.com/infra/infra/+/refs/heads/main/go/src/infra/build/siso/),
  which is meant to be a drop-in Ninja replacement with remote execution
* [Shake](https://shakebuild.com/), a build system in its own right that can
  also execute Ninja files

If you want to see a demo of `ninja_syntax.py` (from upstream Ninja) and
`ninja.py` (from the wild) in action together, check out
[ninja-demo](https://github.com/tekknolagi/ninja-demo).

### Other Ninja-adjacent tools

Check out [Kati](https://github.com/google/kati), which is a Make
implementation that generates Ninja.

Check out [depslint](https://github.com/maximuska/depslint), which can tell you
if you have some missing dependencies in your Ninja file.

Check out [bazel-to-cmake](https://github.com/google/bazel-to-cmake), so you
can write Bazel files that turn into CMake files that turn into Ninja files
that build your code.

Maybe you could even vendor [PocketPy](https://github.com/blueloveTH/pocketpy)
with your code. This would mean you only depend on a C++ compiler to compile
PocketPy, which can run your Python build system, which generates Ninja. It's
not fully featured right now (I had to make some small modifications to
`ninja_syntax.py` to get it to work) but with a little more time it could work
really nicely.

### Build systems papers

Check out [Build Systems à la
Carte](https://www.microsoft.com/en-us/research/uploads/prod/2018/03/build-systems.pdf)
(PDF) if you are feeling adventurous.

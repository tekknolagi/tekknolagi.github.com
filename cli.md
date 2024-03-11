---
title: CLI
layout: page
---

Here are some shell scripts and niceties I use to make my life easier. They are
not offered as packages; they are meant to be copied around.

## hist

I use [`hist.sh`][hist], which I am pretty sure was authored by Carl Shapiro, for
profiling things. It provides a text-based histogram for whatever lines are
piped to it.

[hist]: https://gist.github.com/tekknolagi/e435e35e50a9425f0d3ebf8b7318eae4

```sh
#!/bin/sh
# Likely originally by Carl Shapiro
sort |
uniq -c |
sort -n |
sed 's/^ *//' |
awk 'BEGIN { OFS="\t"} { sum += $1; print sum, $1, substr($0, 1 + index($0, " ")) }' |
sort -n -r |
awk 'BEGIN { IFS=OFS="\t"; print "cum", "pct", "freq" }$1 > max { max = $1 }{ print int($1/max*100), int($2/max*100), $2, $3 }'
```

I'll modify a program like `python` to print out (for example) comma-separated
triples `%s,%s,%s` of slot name, left argument type, and right argument type.
Then I will invoke it like `./modified-python script.py | hist.sh` and the
output looks like:

```console?prompt=$
$ hist < err
cum     pct     freq
100     59      87      NB_ADD,str,str
40      16      24      NB_AND,int,int
23      10      15      NB_INPLACE_ADD,str,str
13      6       9       NB_ADD,int,int
7       2       3       NB_OR,int,int
5       1       2       NB_SUBTRACT,int,int
4       1       2       NB_MULTIPLY,str,int
2       0       1       NB_REMAINDER,str,tuple
2       0       1       NB_LSHIFT,int,int
1       0       1       NB_ADD,tuple,tuple
0       0       1       NB_ADD,bytes,bytes
$
```

Which indicates to me that `str + str` is the most frequently occurring use of
`+` in my script (for example).

## run.py

I use [`run.py`][runpy], which was adapted from a Python program written by
Matthias Braun, to help orchestrate shell stuff from Python. It's a wrapper
around `subprocess.run` that adds a verbose mode, [more enforceable
timeouts][timeouts], and a PTY option.

[runpy]: https://gist.github.com/tekknolagi/3b345cbc7035b8e10e50e7ec54cc7744

[timeouts]: https://bugs.python.org/issue37424

I copy this into projects frequently.

## Ninja generator and small Ninja

It's pretty easy to generate Ninja files and BYOB[^byob] and I wrote a [blog
post](/blog/ninja-is-enough/) about that. Then I discovered someone's very
small single-file Ninja reimplementation in Python and mashed them up into a
[little demo](https://github.com/tekknolagi/ninja-demo).

[^byob]: Build Your Own Bazel

This relies on `ninja_syntax.py` which is copied from the Ninja project.

## Parallel output

Sometimes you have a bunch of tasks to run and want quick-n-dirty
Buck/Bazel-type output. For that, I wrote [`lines.py`][linespy] and a [blog
post](/blog/python-parallel-output/) about it.

[linespy]: https://gist.github.com/tekknolagi/4bee494a6e4483e4d849559ba53d067b

<script async id="asciicast-6xC2Q720qD5xpjNiVRhApzVRx" src="https://asciinema.org/a/6xC2Q720qD5xpjNiVRhApzVRx.js"></script>

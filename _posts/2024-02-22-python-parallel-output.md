---
title: Neat parallel output in Python
layout: post
description: Make parallel tasks print nicely with zero dependencies.
date: 2024-02-22
---

Say you have a program that does some processing of a list (loosely based on a
work-in-progress project):

```python
#!/usr/bin/env python3

def log(repo_name, *args):
    print(f"{repo_name}:", *args)

def randsleep():
    import random
    import time
    time.sleep(random.randint(1, 5))

def func(repo_name):
    log(repo_name, "Starting")
    randsleep()  # Can be substituted for actual work
    log(repo_name, "Installing")
    randsleep()
    log(repo_name, "Building")
    randsleep()
    log(repo_name, "Instrumenting")
    randsleep()
    log(repo_name, "Running tests")
    randsleep()
    log(repo_name, f"Result in {repo_name}.json")

repos = ["repoA", "repoB", "repoC", "repoD"]
map(func, repos)
```

This is fine. It works. It's a little noisy, but it works. But then you get
discover some thing great: your problem is data parallel. That is, you can
process as many repos as your system allows in parallel. Hoorah! You rewrite
using `multiprocessing`:

```python
import multiprocessing

# ...

with multiprocessing.Pool() as pool:
    pool.imap(func, repos, chunksize=1)
```

Unfortunately, the output is a little unwieldy. While each line is still nicely
attributed to a repo, it's spewing lines left and right and the lines are
intermingled. Don't you miss all the beautiful parallel output from tools such
as Buck and Bazel and Cargo?

Fortunately, StackOverflow user [Leedehai][Leedehai] is a terminal pro user and
knows how to rewrite multiple lines at a time in the console. We can adapt that
answer for our needs:

[Leedehai]: https://stackoverflow.com/questions/6840420/rewrite-multiple-lines-in-the-console/59147732#59147732

```python
def log(repo_name, *args):
    with terminal_lock:
        last_output_per_process[repo_name] = " ".join(str(arg) for arg in args)
        sorted_lines = last_output_per_process.items()
        for _ in sorted_lines:
            print("\x1b[1A\x1b[2K", end="")  # move up cursor and delete whole line
        for repo_name, last_line in sorted_lines:
            print(f"{repo_name}: {last_line}")

# ...

with multiprocessing.Manager() as manager:
    last_output_per_process = manager.dict()
    terminal_lock = manager.Lock()
    with multiprocessing.Pool() as pool:
        pool.imap(func, repos, chunksize=1)
```

This will print each item's status, one line at a time, to the terminal. It
will print in the order that the item is added to `last_output_per_process`,
but you can change that by (for example), sorting alphanumerically:
`sorted(last_output_per_process.items())`.

Note that we have to lock both the data structure and the terminal output to
avoid things getting mangled; they are shared (pickled, via `Manager`) between
processes.

I'm not sure what this does if the log output is multiple lines long or if
someone else is mucking with `stdout`/`stderr` (a stray `print`, perhaps).
Please write in if you find out or have neat solutions.

Last, you can limit maximum output length to the number of active processes, by
`del`ing from `last_output_per_process` at `func` exit.

This technique is probably fairly portable to any programming language that has
threads and locks. The key difference is those implementations should use
threads instead of processes; I did processes because it's Python.

## A demo for you

Since you read this far, here is a demo of the program as it is written:

<script async id="asciicast-Xgwj7Jpk3nWUM596jjH2jWay5" src="https://asciinema.org/a/Xgwj7Jpk3nWUM596jjH2jWay5.js"></script>

and also with the program cleaning up processes as they finish (`del
last_output_per_process[repo_name]` at the end of `func` but remember to lock):

<script async id="asciicast-ipLlGw70veSS7UARJUdyYs4pG" src="https://asciinema.org/a/ipLlGw70veSS7UARJUdyYs4pG.js"></script>

Enjoy your newfound fun output!

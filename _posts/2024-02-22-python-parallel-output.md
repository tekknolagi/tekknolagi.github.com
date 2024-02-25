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
for repo in repos:
    func(repo)
```

This is fine. It works. It's a little noisy, but it works. But then you
discover something great: your problem is data parallel. That is, you can
process as many repos as your system allows in parallel. Hoorah! You rewrite
using `multiprocessing`:

```python
import multiprocessing

# ...

with multiprocessing.Pool() as pool:
    pool.map(func, repos, chunksize=1)
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
def fill_output():
    to_fill = num_lines - len(last_output_per_process)
    for _ in range(to_fill):
        print()

def clean_up():
    for _ in range(num_lines):
        print("\x1b[1A\x1b[2K", end="")  # move up cursor and delete whole line

def log(repo_name, *args):
    with terminal_lock:
        last_output_per_process[repo_name] = " ".join(str(arg) for arg in args)
        clean_up()
        sorted_lines = last_output_per_process.items()
        for repo_name, last_line in sorted_lines:
            print(f"{repo_name}: {last_line}")
        fill_output()

def func(repo_name):
    # ...
    with terminal_lock:
        del last_output_per_process[repo_name]

# ...

repos = ["repoA", "repoB", "repoC", "repoD"]
num_procs = multiprocessing.cpu_count()
num_lines = min(len(repos), num_procs)
with multiprocessing.Manager() as manager:
    last_output_per_process = manager.dict()
    terminal_lock = manager.Lock()
    fill_output()
    with multiprocessing.Pool() as pool:
        pool.map(func, repos, chunksize=1)
    clean_up()
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

This technique is probably fairly portable to any programming language that has
threads and locks. The key difference is those implementations should use
threads instead of processes; I did processes because it's Python.

Check out the expanded version [in this
Gist](https://gist.github.com/tekknolagi/4bee494a6e4483e4d849559ba53d067b).

## A demo for you

Since you read this far, here is a demo of the program:

<script async id="asciicast-6xC2Q720qD5xpjNiVRhApzVRx" src="https://asciinema.org/a/6xC2Q720qD5xpjNiVRhApzVRx.js"></script>

Enjoy your newfound fun output!

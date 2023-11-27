---
title: "Building a small REPL in Python"
layout: post
date: 2023-11-27
---

In a lot of my previous interpreter/compiler projects written in Python I
hand-rolled a REPL. It turns out that Python comes with a bunch of batteries
included and this is totally unnecessary---you get a lot of goodies for free.
Let's take a look at how to use them, starting from embedding a normal Python
REPL in your project.

<!-- TODO: define REPL -->

## Programmatic control of Python REPL

The bare minimum is controlling a Python REPL from inside Python. It's only a
couple of lines:

```python
#!/usr/bin/env python3
# repl.py
import code


repl = code.InteractiveConsole()
repl.interact()
```

If you (`chmod +x repl.py` and) run this, you get what looks like a normal
Python REPL, plus a little extra output:

```console
$ ./repl.py
Python 3.10.12 (main, Jun 11 2023, 05:26:28) [GCC 11.4.0] on linux
Type "help", "copyright", "credits" or "license" for more information.
(InteractiveConsole)
>>> 1 + 2
3
>>> ^D
now exiting InteractiveConsole...
$
```

(I hit Control-D to exit.)

Let's say we don't want the output. We can squash that first by specifying the
banner, which is the topmost bit:

```python
# repl.py
import code


repl = code.InteractiveConsole()
repl.interact(banner="")
```

This removes all of the usual Python preamble.

```console
$ ./repl.py
>>> ^D
now exiting InteractiveConsole...
$
```

We can also remove specify the exit message or completely silence it:

```python
# repl.py
import code


repl = code.InteractiveConsole()
repl.interact(banner="", exitmsg="")
```

And now we have a much quieter experience:

```console
$ ./repl.py
>>> ^D
$
```

Right. But that's not very interesting. Let's remove the Python bits so we can
enter code written in our own programming language.

## Making it your own

To integrate our own interpreter or compiler, we subclass `InteractiveConsole`
and override the `runsource` method. We are just printing whatever input we
receive, but you could wire it up to your lexer/parser/... at this junction:

```python
import code


class Repl(code.InteractiveConsole):
    def runsource(self, source, filename="<input>", symbol="single"):
        # TODO: Integrate your compiler/interpreter
        print("source:", source)


repl = Repl()
repl.interact(banner="", exitmsg="")
```

Take a look:

```console
$ ./repl.py
>>> 1 + 2
source: 1 + 2
>>> ^D
$
```

It works! You could stop here. But you might want input over multiple lines,
and what we have now just operates over lines.

## Adding continuations

To indicate to the caller of `runsource` that you are waiting for more input,
perhaps until a statement-ending semicolon (for example), `return True`:

```python
class Repl(code.InteractiveConsole):
    def runsource(self, source, filename="<input>", symbol="single"):
        # TODO: Integrate your compiler/interpreter
        if not source.endswith(";"):
            return True
        print("source:", source)
```

This will bring up the familiar "ps2" prompt until your input ends with a
semicolon:

```console
$ ./repl.py
>>> 1 +
... 2;
source: 1 +
2;
>>> 
$
```

Very nice. You might do this by having your parser drive your lexer, or
detecting "Unexpected EOF" errors in your parser, or something else entirely.

This is another perfectly fine cut point. But you might be wondering: how hard
is it to add line editing support? The arrow keys do not work right now. And
it's not hard at all!

## Adding readline support

Just import `readline`.

This gives you up/down navigation, Emacs-like line navigation, etc.

```python
import code
from types import ModuleType
from typing import Optional


readline: Optional[ModuleType]
try:
    import readline
except ImportError:
    readline = None


# ...
```

We want to have types and also use the module later so there's a bunch of
typing machinery.

## Adding history

```python
import os

# ...

REPL_HISTFILE = os.path.expanduser(".myreplname-history")  # arbitrary name
REPL_HISTFILE_SIZE = 1000
if readline and os.path.exists(REPL_HISTFILE):
    readline.read_history_file(REPL_HISTFILE)

repl = Repl()
repl.interact(banner="", exitmsg="")

if readline:
    readline.set_history_length(REPL_HISTFILE_SIZE)
    readline.write_history_file(REPL_HISTFILE)
```

## Adding tab completion

<!-- TODO: atexit?? -->

```python
from typing import Dict, List
# ...

class Completer:
    def __init__(self, env: Dict[str, object]) -> None:
        self.env: Dict[str, object] = env
        self.matches: List[str] = []

    def complete(self, text: str, state: int) -> Optional[str]:
        if state == 0:
            # Is it important that they are sorted lexicographically? Or just
            # have a stable order? Or...?
            options = sorted(self.env.keys())
            if not text:
                self.matches = options[:]
            else:
                self.matches = [key for key in options if key.startswith(text)]
        try:
            return self.matches[state]
        except IndexError:
            return None

env = {"add": lambda x, y: x + y, "abs": abs}  # some builtins or something
if readline:
    readline.set_completer(Completer(env).complete)
    readline.parse_and_bind("tab: complete")  # or menu-complete

repl = Repl()
repl.interact(banner="", exitmsg="")
```

```console
hickory% /tmp/repl.py
>>> a[^tab]
abs  add
>>> a
```

## Changing the prompt

<!-- TODO: do you have to change sys.ps1 and sys.ps2? -->

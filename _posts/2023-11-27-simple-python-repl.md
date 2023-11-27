---
title: "Building a small REPL in Python"
layout: post
date: 2023-11-27
---


## Programmatic control of Python REPL

```python
# repl.py
import code


repl = code.InteractiveConsole()
repl.interact()
```

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

```python
# repl.py
import code


repl = code.InteractiveConsole()
repl.interact(banner="")
```

```console
$ ./repl.py
>>> ^D
now exiting InteractiveConsole...
$
```

```python
# repl.py
import code


repl = code.InteractiveConsole()
repl.interact(banner="", exitmsg="")
```

```console
$ ./repl.py
>>> ^D
$
```

## Making it your own

```python
import code


class Repl(code.InteractiveConsole):
    def runsource(self, source, filename="<input>", symbol="single"):
        # TODO: Integrate your compiler/interpreter
        print("source:", source)


repl = Repl()
repl.interact(banner="", exitmsg="")
```

```console
$ ./repl.py
>>> 1 + 2
source: 1 + 2
>>> ^D
$
```

## Adding readline support

This gives you up/down navigation, Emacs-like line navigation, etc.

Just import `readline`.

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
```

```console
hickory% /tmp/repl.py
>>> a[^tab]
abs  add
>>> a
```

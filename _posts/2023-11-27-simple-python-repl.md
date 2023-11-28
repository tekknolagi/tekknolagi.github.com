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

We already have `readline` and that supports reading/writing history files if
we wire it up. So wire it up.

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

Now you should be able to use the up arrow key in a new session to see what
your previous session contained. Or Control-R, even.

And now for the most exciting thing, maybe: tab completion.

## Adding tab completion

It's helpful for learning about new tools and it can make you more efficient.
So let's add tab completion.

The `readline` API expects to be able given a state machine function that it
can call multiple times in a row with different states. Most implementations I
have seen of this online use a class, but you could also use nested functions
or something like that.

We'll make a `Completer` class that has a `complete` method. `readline` calls
this with increasing `state`s, starting at 0. So if we are in state 0, we
initialize our potential matches before returning the current match. If we are
in any other state, we can just return the current match.

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
            # Some implementations check if text.strip() is empty but I can't
            # figure out how to get text to start or end with whitespace.
            self.matches = sorted(key for key in self.env.keys() if key.startswith(text))
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

Let's play around with it.

```console
hickory% /tmp/repl.py
>>> [^tab]
abs  add
>>> a[^tab]
abs  add
>>> a
```

Nice.

An important note: it seems like `readline` swallows any exceptions raised in
your `complete` function, so this makes debugging a little tricky (bugs just
result in autocomplete failing!). To combat this, I added a bunch of `print`s
in development.

## Changing the prompt

<!-- TODO: do you have to change sys.ps1 and sys.ps2? -->

<!-- TODO atexit history
https://stackoverflow.com/questions/9468435/how-to-fix-column-calculation-in-python-readline-if-using-color-prompt/9468954#9468954
-->

<!-- TODO color prompts
https://stackoverflow.com/questions/9468435/how-to-fix-column-calculation-in-python-readline-if-using-color-prompt/9468954#9468954
-->

## Now what

Go forth and either integrate this into your existing interpreter/compiler or
write a little interpreter just for fun.

## Why not use cmd

I found the [`cmd`](https://docs.python.org/3.10/library/cmd.html) module
midway through this post and thought I might be reinventing the wheel again.
But it turns out that `cmd.Cmd`, while it does provide you with some niceties,
does not give anywhere near the same amount of flexibility and also generally
requires a static list of commands---the expectation is that you write a bunch
of `do_X` methods. You can sidestep that by overriding `onecmd` but then you
still don't get multi-line editing out of the box. You can customize the prompt
more neatly that overwriting `sys.ps1`, though.

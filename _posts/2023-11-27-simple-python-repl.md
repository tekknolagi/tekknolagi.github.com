---
title: "Building a small REPL in Python"
layout: post
description: Don't go it alone. Take this!
date: 2023-11-27
---

In a lot of my previous interpreter/compiler projects written in Python I
hand-rolled a REPL (read-eval-print-loop). It turns out that Python comes with
a bunch of batteries included and this is totally unnecessary---you get a lot
of goodies for free. Let's take a look at how to use them, starting from
embedding a normal Python REPL in your project.

I wrote this post as I finally figured all this stuff out for an unreleased
runtime for a new content-addressable language. Keep an eye on this space...

Take a look at the [`code`](https://docs.python.org/3/library/code.html) docs
as you follow along.

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

We can also specify the exit message or completely silence it:

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
receive, but you could wire it up to your lexer/parser/... at this point:

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
and what we have now just operates over single lines.

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
is it to add line editing support? The arrow keys do not work right now. It's
not hard at all!

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

If you don't care about types, you can drop all the machinery and just `import readline`.

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
            options = (key for key in self.env.keys() if key.startswith(text))
            self.matches = sorted(options)
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

### What is a name?

The `readline` library comes with a default notion of what constitutes
delimiters in the input. If you have a funky programming language that does not
share the same notion of identifiers as C, you may need to change the
delimiters. In my case, I wanted `$` to be a valid part of identifiers, so I
went a little nuts and said "just try and complete until whitespace".

```python
# what determines the end of a word; need to set so $ can be part of a
# variable name
readline.set_completer_delims(" \t\n")
```

I am not sure if this is ideal. I think it only matters for the names you say
are matches in your completion function. You may want a stricter set to exclude
(for example) quotation marks, etc.

## Changing the prompt

In order to change the prompt, we need to modify globals in the `sys` module.
`ps1` is for the normal prompt, and `ps2` is for the continuation prompt.

```python
import sys

# ...

sys.ps1 = "> "
sys.ps2 = ". "

repl = Repl()
repl.interact(banner="", exitmsg="")
```

This is a little gross but it's the only way to customize the prompt, as
`InteractiveConsole.interact` directly reads from `sys`[^override-globals]. And
overriding `interact` defeats the purpose of the exercise since it has a fair
bit of helpful logic in it. Maybe one day I will submit a pull request to allow
custom prompts via parameters or something.

[^override-globals]: I wonder if it's possible to make a custom `interact` in
    our subclass which is just a copy of `InteractiveConsole.interact` with its
    `__globals__` replaced to point to hacked-up `sys` that contains our `ps1`
    and `ps2`. This is *not* nice-looking either, but avoids the global
    patching.

    (...some time later...)

    Turns out, yes, it's possible. You can use `copy_func` from [this
    StackOverflow answer](https://stackoverflow.com/a/49077211/569183) to get
    this monstrosity:

    ```python
    import copy
    import types
    import functools


    def copy_func(f, globals=None, module=None):
        """Based on https://stackoverflow.com/a/13503277/2988730 (@unutbu)"""
        if globals is None:
            globals = f.__globals__
        g = types.FunctionType(f.__code__, globals, name=f.__name__,
                               argdefs=f.__defaults__, closure=f.__closure__)
        g = functools.update_wrapper(g, f)
        if module is not None:
            g.__module__ = module
        g.__kwdefaults__ = copy.copy(f.__kwdefaults__)
        return g


    class MySys:
        def __init__(self):
            self.ps1 = "> "
            self.ps2 = ". "


    class Repl(code.InteractiveConsole):
        # ...
        interact = copy_func(code.InteractiveConsole.interact, globals={"sys": MySys()})
    ```

    Neat? I guess?

<!-- TODO atexit history
https://stackoverflow.com/questions/9468435/how-to-fix-column-calculation-in-python-readline-if-using-color-prompt/9468954#9468954
-->

<!-- TODO color prompts
https://stackoverflow.com/questions/9468435/how-to-fix-column-calculation-in-python-readline-if-using-color-prompt/9468954#9468954
-->

## Now what

Go forth and either integrate this into your existing interpreter/compiler or
write a little interpreter just for fun.

## Other ideas

What about syntax highlighting as you type? That's become a popular thing to do
these days.

Add [undo support](https://ballingt.com/interactive-interpreter-undo/) with
`fork`!

Do something else [after the REPL
exits](https://stackoverflow.com/a/36868625/569183).

## Why not use cmd

I found the [`cmd`](https://docs.python.org/3/library/cmd.html) module
midway through this post and thought I might be reinventing the wheel again.
But it turns out that `cmd.Cmd`, while it does provide you with some niceties,
does not give anywhere near the same amount of flexibility and also generally
requires a static list of commands---the expectation is that you write a bunch
of `do_X` methods. You can sidestep that by overriding `onecmd` but then you
still don't get multi-line editing out of the box. You can customize the prompt
more neatly that overwriting `sys.ps1`, though.

<!-- TODO add tests for repl with mocking stdin/stdout
https://news.ycombinator.com/item?id=38449933 -->

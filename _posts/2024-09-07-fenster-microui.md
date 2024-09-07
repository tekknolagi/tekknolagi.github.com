---
title: "microui+fenster=small gui"
date: 2024-09-07
---

Sometimes I just want to put pixels on a screen. I don't want to think about
SDL this or OpenGL that---I just want to draw my pixel buffer and be done.

[fenster](https://github.com/zserge/fenster), a tiny 2D canvas library by Serge
Zaitsev, does just that. It's a tiny drop-in header-only C/C++ file that weighs
no more than 400 LOC of pretty readable code. It works with WinAPI, Cocoa, and
X11. And it handles keyboard and mouse input, too!

Sometimes I want to do just a little more than draw pixels---maybe have a menu,
some buttons, render text---and I don't want to completely DIY but I still
don't want to think about SDL.

Fortunately, [microui](https://github.com/rxi/microui) by rxi exists and
handles the translation from GUI elements into a simple retargetable drawing
bytecode. It's similarly a small, drop-in library, weighing only 1500 LOC.

Unfortunately, the demo program uses SDL as a backend for the bytecode. I'd
been meaning to see if I could instead use fenster but understanding what a
"quad" was or what "glScissor" did seemed intimidating. The project went
nowhere.

Then, as usual, [Kartik](https://akkartik.name/) and I had a small argument and
that resulted in us creating the fenster backend for microui! I sent him a
skeleton to show what I wanted to do and he did most of the heavy lifting for
the OpenGL-like parts.

The result is a less than 250 LOC file that binds microui to fenster. It's
inspired by the SDL renderer demo, but with a couple of added functions to
abstract away keys and mouse buttons. It's hacky and there's some stuff we
still don't understand, but it works! And by "works" I mean draws the expected
demo windows, handles mouse hover and click, and handles keyboard input.

Things left to figure out:

* How to determine when to render from the texture and when from the provided
  drawing command's color
* Mod keys like so that, for example, <kbd>Shift</kbd>+<kbd>1</kbd> renders <kbd>!</kbd>
* Scrolling

Check it out [here](https://github.com/tekknolagi/full-beans). It's designed to
all be dropped directly into your project.

<img src="/assets/img/fenster-microui.png" alt="microui+fenster demo window in X11" width="100%"/>

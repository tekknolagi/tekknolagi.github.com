---
layout: page
title: Programming languages resources
---

This page is a collection of my favorite resources for people getting started
writing programming languages. I hope to keep it updated as long as I continue
to find great stuff.

## Compilers

* Tufts [compilers course](http://www.cs.tufts.edu/~sguyer/classes/comp181-2006/)
  (2006, but it's been taught more recently. I should probably ping Sam.)
* Abdulaziz Ghuloum's [minimal Scheme to x86 compiler](/assets/img/11-ghuloum.pdf)
* Nora Sandler's [minimal C compiler](https://norasandler.com/2017/11/29/Write-a-Compiler.html)
* Jack Crenshaw's [let's build a compiler](https://compilers.iecc.com/crenshaw/)
* [Recursive descent parsing](http://web.archive.org/web/20170712044658/https://ryanflannery.net/teaching/common/recursive-descent-parsing/)
  in C. Note that this just verifies the input string, and more has to be done
  to build a tree out of the input.
* Vidar Hokstad's [Writing a compiler in Ruby, bottom up](http://hokstad.com/compiler)
* My [adaptation](https://bernsteinbear.com/blog/compiling-a-lisp-0/) of
  Ghuloum's paper
* Rui Ueyama's [chibicc](https://github.com/rui314/chibicc), a C compiler in
  the Ghuloum style

## Lisp specific

* kanaka's [mal](https://github.com/kanaka/mal)
* leo (lwh)'s [Building LISP](https://www.lwh.jp/lisp/)
* Peter Michaux's [Scheme from Scratch](http://peter.michaux.ca/articles/scheme-from-scratch-introduction)
* Daniel Holden's [Build Your Own Lisp](http://buildyourownlisp.com/contents)
* Anthony C. Hay's fairly readable [Lisp interpreter in 90 lines of C++](http://howtowriteaprogram.blogspot.com/2010/11/lisp-interpreter-in-90-lines-of-c.html)
* My own [Writing a Lisp](https://bernsteinbear.com/blog/lisp/) blog post
  series
* carld's [Lisp in less than 200 lines of C](https://carld.github.io/2017/06/20/lisp-in-less-than-200-lines-of-c.html)
* UTexas's [A simple scheme compiler](https://www.cs.utexas.edu/ftp/garbage/cs345/schintro-v14/schintro_142.html#SEC271)
* Rui Ueyama's [minilisp](https://github.com/rui314/minilisp)
* The [Bones](http://www.call-with-current-continuation.org/bones/) Scheme
  compiler

## Runtimes

* munificent's [Crafting Interpreters](https://craftinginterpreters.com/) book
* Mario Wolczko's [CS 294-113](http://www.wolczko.com/CS294/), a course on
  managed runtimes
* My own [bytecode compiler/VM](https://bernsteinbear.com/blog/bytecode-interpreters/)
  blog post
* Justin Meiners and Ryan Pendelton's [Write your own virtual machine](https://justinmeiners.github.io/lc3-vm/)

### Pointer tagging and NaN boxing

Resources on representing small values efficiently.

* nikic's [Pointer magic...](https://nikic.github.io/2012/02/02/Pointer-magic-for-efficient-dynamic-value-representations.html)
* Sean's [NaN-Boxing](https://sean.cm/a/nan-boxing)
* zuiderkwast's [nanbox](https://github.com/zuiderkwast/nanbox)
* albertnetymk's [NaN Boxing](http://albertnetymk.github.io/2016/08/06/nan_boxing/)
* Ghuloum's [Incremental approach](http://scheme2006.cs.uchicago.edu/11-ghuloum.pdf)
  (PDF), which introduces this in a compiler setting
* Chicken Scheme's [data representation](https://wiki.call-cc.org/man/4/Data%20representation)
* Guile Scheme's [Faster Integers](https://www.gnu.org/software/guile/manual/html_node/Faster-Integers.html)
* Femtolisp [object implementation](https://github.com/JeffBezanson/femtolisp/blob/master/flisp.h)
* Leonard Schütz's [NaN Boxing](https://leonardschuetz.ch/blog/nan-boxing/) article

### Just-In-Time compilers

Small JITs to help understand the basics. Note that these implementations tend
to focus on the compiling ASTs or IRs to machine code, rather than the parts of
the JIT that offer the most performance: inline caching and code inlining.
Compiling is great but unless you're producing good machine code, it may not do
a whole lot.

* Antonio Cuni's [jit30min](https://github.com/antocuni/jit30min)
* Christian Stigen Larsen's [Writing a basic x86-64 JIT compiler from scratch in stock Python](https://csl.name/post/python-jit/)
* My very undocumented (but hopefully readable)
  [implementation](https://github.com/tekknolagi/ghuloum) of the Ghuloum
  compiler

## Game Boy Emulators

* The [Pan Docs](http://problemkaputt.de/pandocs.htm) (newer version
  [here](https://gbdev.io/pandocs/)?), which give technical data about the Game
  Boy hardware, I/O ports, flags, cartridges, memory map, etc
* This excellent [explanation](https://realboyemulator.wordpress.com/2013/01/03/a-look-at-the-game-boy-bootstrap-let-the-fun-begin/)
  of the boot ROM
* This [opcode table](https://www.pastraiser.com/cpu/gameboy/gameboy_opcodes.html)
  that details the full instruction set, including CB opcodes
* This [full opcode reference](https://rednex.github.io/rgbds/gbz80.7.html) for
  the GBZ80
* The [Game Boy CPU manual](http://marc.rawer.de/Gameboy/Docs/GBCPUman.pdf)
* The [GameBoy memory map](http://gameboy.mongenel.com/dmg/asmmemmap.html)
* This [blog post](https://mattbruv.github.io/gameboy-crust/) that gives a
  pretty simple state machine for the different rendering steps
* The [Ultimate Game Boy Talk](https://www.youtube.com/watch?v=HyzD8pNlpwI) by
  Michael Steil at CCC
* This [ROM generator](http://catskull.net/GB-Logo-Generator/) for custom logos
* This [sample DAA implementation](https://www.reddit.com/r/EmuDev/comments/cdtuyw/gameboy_emulator_fails_blargg_daa_test/etwcyvy/)
* This [awesome-gbdev](https://github.com/gbdev/awesome-gbdev) list
* This [excellent emulator and debugger](https://github.com/drhelius/Gearboy)
* Another [emulator and debugger](https://github.com/Jonazan2/PatBoy)
* The [Game Boy complete technical reference](https://gekkio.fi/files/gb-docs/gbctr.pdf)
* This [Gameboy Overview](https://thomas.spurden.name/gameboy/)
* blargg's [test ROMs](https://gbdev.gg8.se/files/roms/blargg-gb-tests/)
  which have instruction tests, sound tests, etc
* gekkio's [emulator](https://github.com/Gekkio/mooneye-gb#accuracy-comparison)
  and his [test ROMs](https://github.com/Gekkio/mooneye-gb/tree/master/tests)
* This [fairly readable Go emulator](https://github.com/Humpheh/goboy),
  which has helped me make sense of some features
* This [fairly readable C emulator](https://github.com/simias/gaembuoy)
* This [fairly readable C++ implementation](https://github.com/MoleskiCoder/EightBit/blob/master/LR35902/src/LR35902.cpp)
* This [helpful GPU implementation in Rust](https://github.com/mattbruv/Gameboy-Crust/blob/master/src/core/gpu.rs)
* This [reference](https://gb-archive.github.io/salvage/decoding_gbz80_opcodes/Decoding%20Gamboy%20Z80%20Opcodes.html)
  for decoding GameBoy instructions.
  * NOTE: This has one [bug](https://github.com/gb-archive/salvage/issues/1)
    that someone and I independently found. The [original repo](https://github.com/phire/Kea)
    has fixed the bug but not the page linked above.
* This [summary blog post](https://dandigit.com/posts/bigboy-writing-a-gameboy-emulator)
  explaining GPU modes
* And of course [/r/emudev](https://old.reddit.com/r/EmuDev/)

[This](https://nullprogram.com/blog/2017/11/03/) is a potentially fun way to
render the screen without SDL, but only for non-interactive purposes.

This [YouTube](https://www.youtube.com/playlist?list=PLye7LM1YVhDHR4TGMklN3tMt_J2jIrn1w)
playlist looks like it could be worth a watch, but it's a lot of hours.

## Lists

> I should probably pick and choose some great stuff from these lists to copy
> onto this page.

* [awesome compilers resources](https://github.com/aalhour/awesome-compilers)
* [DIY emulator/VM resources](https://github.com/danistefanovic/build-your-own-x#build-your-own-emulator--virtual-machine)
* [programming language resources](https://github.com/danistefanovic/build-your-own-x#build-your-own-programming-language)

## Communities

* [/r/programminglanguages](https://reddit.com/r/programminglanguages)
* [/r/compilers](https://reddit.com/r/compilers)

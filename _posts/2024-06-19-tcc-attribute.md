---
title: "TCC does support __attribute__(section(...)), actually"
layout: post
date: 2024-06-19
---

If you are using TCC/TinyCC and you are trying to get it to place code or data
in a specific section with `__attribute__((section("...")))` and it is not
creating the section, or if you are trying to get the start/end of the section
with `__start_SECTION` and `__stop_SECTION` and you are getting linker errors
about undefined symbols, don't worry. It's not you. And it's not exactly TCC,
either.

```console?prompt=$
$ gcc const.c && ./a.out
f: 0x5c97a966d020, g: 0x5c97a966d030
start: 0x5c97a966d048, end 0x5c97a966d050
$ clang const.c && ./a.out
f: 0x574924409030, g: 0x574924409040
start: 0x574924409068, end 0x574924409070
$ tcc const.c
tcc: error: undefined symbol '__stop_const_heap'
tcc: error: undefined symbol '__start_const_heap'
$
```

The fix is to add this snippet to the top of your code (after including the
glibc headers):

```c
#ifdef __TINYC__
#undef __attribute__
#endif
```

and then it will magically start working.

It turns out that this is because glibc headers define `__attribute__` to *do
nothing* if the compiler either does not pretend to be GCC with `__GNUC__` or
if it is an old version of GCC. This means it gets stripped out by the
preprocessor, so the compiler never sees it. To undo that, we `#undef
__attribute__`, which passes through `__attribute__` annotations to the
compiler.

I found this out from [Allan
Wind](https://stackoverflow.com/a/78639523/569183), who found it out from the
[tcc-devel mailing list](https://lists.nongnu.org/archive/html/tinycc-devel/2018-04/msg00008.html).

I found no mention of this in my searches, so hopefully this post will help
anyone who runs into this in the future. Perhaps they can avoid scrolling
through TCC source code, downloading TCC, instrumenting it, running GDB on
it... and of course, calling in [Tom Hebb](https://tchebb.me/).

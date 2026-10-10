---
title: "The big optimizer in the sky"
layout: post
---

Consider the optimizer. It sits in your compiler pipeline and thanklessly does
its job all day long. It doesn't wonder why most of the code you are compiling
is the same. It goes in, quickly does an impressive suite of optimizations, and
leaves.

See how LLVM's optimizer makes short work of this loop[^another-popcount]:

```c
unsigned count_one_bits(unsigned x) {
  unsigned result = 0;
  while (x) {
    ++result;
    x &= x - 1;
  }
  return result;
}
```

turning it into one instruction:

```
0000000000000000 <count_one_bits>:
   0:   f3 0f b8 c7             popcnt eax,edi
   4:   c3                      ret
```

[^another-popcount]: I wonder why it doesn't recognize this loop:

    ```c
    unsigned count_one_bits(unsigned x) {
      unsigned result = 0;
      while (x) {
        if (x & 1) result++;
        x >>= 1;
      }
      return result;
    }
    ```

    I know the idiom recognizer is pretty specific but this loop seems like the
    more natural loop to write at first, even if it has an extra branch.

Marvelous. Of course, it does a lot more: codebases grow past one little
function into sprawling morasses with thousands or millions of lines, many
dependencies, and multiple source languages.

Still, the optimizer is relentless: when asked, it dutifully compiles all of
this code, over. And over. And over again.

Often the optimizer gets a break: people don't often run `clang++ *.cpp` every
time. Since a given change likely only modifies one or two files, and most
source files are independent from one another, we have separate compilation.
Each source file can be compiled in isolation into its own object file, and
later linked together. This process is irritating to do by hand, so we have
build tools such as Make to do it for us. Make can use the last modification
times of the source and object files to determine if the source file needs
compiling.

The story gets better. If we have properly specified our inter-file
dependencies, the compilation process can be parallelized across multiple
cores. Make comes with a built-in jobserver accessible via `make -jN`. Use all
of your cores.

Heck, even linking is parallel now. Ruy Ueyama of LLD and chibicc fame wrote
the [mold linker](https://github.com/rui314/mold/), which is a mostly drop-in
replacement that can parallelize your link stage. One time I worked on a
project that had 45 minute (or more) link times. That gave me enough time to
make a custom animated GIF about my frustration. I could really have used mold
that day.

Still, if you are compiling a project across multiple directories, or
frequently switching version control branches, this stuff kind of falls apart.
Changing m-times causes frequent invalidation, requiring re-builds of stuff
that you just compiled three minutes ago. That is, unless you use
[ccache](https://github.com/ccache/ccache), which caches compilation artifacts
by *file contents* and *compiler flags* (instead of m-time) in one well-known
location on your computer. I love ccache. It even comes with a drop-in wrapper
that you can use to replace your compiler invocation, even in some existing
build system.

At some point, though, you hit the limit of your local machine. When a project
gets big enough, probably most parts of it will ossify and you will only change
a couple small components at a time. But your colleague might be changing a
different set of components, so you start to wonder if it's possible to share
work with your colleagues. This is where [distcc]() and [icecc]() and
[icecream]() come in.

Google and Facebook have their internal build systems (Blaze and Buck,
respectively),

But still, *still*, these all seem like very coarse-grained approaches. At the
end of the day, each of the tools described above is some abstraction over
avoid re-compiling a single file. If something in that file changes, the whole
file must be re-compiled. Furthermore, if some code is shared between files in
the same project---even something like inlined code from a header file---then
that code will get re-optimized and re-compiled for each instance.

Maybe that's desirable---after all, context makes optimizers go round---but
maybe it's not. Perhaps we actually want to start sharing work across
compilation units. Or across projects. Or even across programming languages.
What would that even mean?

## Sharing results

Dependencies, graphs

## Incremental improvements

But first, an aside about e-graphs.

Egraphs and rebuild

## Trust and verification

## Ownership and maintainership

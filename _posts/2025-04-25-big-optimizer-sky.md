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
by *file contents* in one well-known location on your computer. I love ccache.

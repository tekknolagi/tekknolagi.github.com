---
title: "Compiling a Lisp: The smallest program"
layout: post
date: 2020-08-29 20:49:00 PDT
description: A small mmap demo
og_image: /assets/img/compiling-a-lisp-og-image.png
series: compiling-a-lisp
---

<span data-nosnippet>
*[previous](/blog/compiling-a-lisp-0/)*
</span>

Welcome to the first post in the "Compiling a Lisp" series. We're going to
write a small program today. Before we actually compile anything, though, let's
build up a bit of a foundation for code execution. That way, we can see the
code compile *and* run *and* be satisfied with the results of both.

Instead of compiling to disk, like most compilers you may be familiar with
(GCC, Clang, DMD, Python, etc), we're going to compile in memory. This means
that every time we run the program we have to compile it again, but it also
means we don't have to deal with whatever on-disk format an executable has to
be on your platform (ELF, Mach-O, etc). We can just point the processor at the
code and say "go". This style of compilation is known as "Just-in-Time"
compilation, because the compilation happens right when you need it, and not
before[^1].

Let's start with a small demo.

```c
#include <assert.h>   /* for assert */
#include <stddef.h>   /* for NULL */
#include <string.h>   /* for memcpy */
#include <sys/mman.h> /* for mmap and friends */

const unsigned char program[] = {
    // mov eax, 42 (0x2a)
    0xb8, 0x2a, 0x00, 0x00, 0x00,
    // ret
    0xc3,
};

const int kProgramSize = sizeof program;

typedef int (*JitFunction)();

int main() {
  void *memory = mmap(/*addr=*/NULL, /*length=*/kProgramSize,
                      /*prot=*/PROT_READ | PROT_WRITE,
                      /*flags=*/MAP_ANONYMOUS | MAP_PRIVATE,
                      /*filedes=*/-1, /*offset=*/0);
  memcpy(memory, program, kProgramSize);
  int result = mprotect(memory, kProgramSize, PROT_EXEC);
  assert(result == 0 && "mprotect failed");
  JitFunction function = *(JitFunction*)&memory;
  int return_code = function();
  assert(return_code == 42 && "the assembly was wrong");
  result = munmap(memory, kProgramSize);
  assert(result == 0 && "munmap failed");
  return return_code;
}
```

This C program:

1. Allocates writable memory (`mmap`)
1. Copies a program into it (`memcpy`)
1. Makes the memory executable (`mprotect`)
1. Calls the memory as a function
1. Deallocates memory (`munmap`)

The order of those steps is important! This C program will fail, usually with a
segmentation fault, if you mix them up or skip one of them.

If you want to understand the pointer shenanigans see the footnote[^2], but if
you would like to ignore it and pretend I never did that please keep reading.
The program works, though:

```
sequoia% gcc -Wall -Wextra -pedantic -fno-strict-aliasing mmap-demo.c
sequoia% ./a.out 
sequoia% echo $?
42
sequoia%
```

Let's back up and go through that demo line-by-line. I'll skip the includes
since that's just part of life in C.

### The machine code

First let's take a look at our program. Here we have some raw machine code
encoded as hex bytes, with helpful commentary by yours truly explaining what
the bytes mean in human-speak.

```c
const unsigned char program[] = {
    // mov eax, 42 (0x2a)
    0xb8, 0x2a, 0x00, 0x00, 0x00,
    // ret
    0xc3,
};
```

I generated this code by going to the [Compiler Explorer](https://godbolt.org),
making the compiler compile to binary, and typing in a C program that just
returns `42`[^3].

This is as good a method as any for doing some initial research for what
instructions you want to emit. You'll have to look a little further afield
(like in this [quick reference](https://c9x.me/x86/) or the [official Intel
x86-64
manual](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-instruction-set-reference-manual-325383.pdf))
if you want to figure out how to encode instructions without manually having a
table for all the variations you want. We'll touch more on that later.

In this machine code, `0xb8` is the instruction for "move the following 32-bit
integer to the register `eax`". It's a special case of the `mov` instruction.
`eax` is (the lower half of) one of several [general-purpose
registers](http://web.stanford.edu/class/cs107/guide/x86-64.html) in x86-64. It
is also the register conventionally used for return values, but that could vary
between [calling
conventions](https://en.wikipedia.org/wiki/X86_calling_conventions). It's not
important to know all the details of every calling convention, but it *is*
important to know that a calling convention is just that --- a convention. It
is an agreement between the people who write functions and the people who call
functions about how data gets passed around. In this case, we are moving `42`
into `eax` because `eax` is the return register in the [System V
AMD64](https://en.wikipedia.org/wiki/X86_calling_conventions#System_V_AMD64_ABI)
calling convention (used on macOS, Linux, other Unices these days) and because
we're calling this hand-built function from C like any other function. It needs
to be a well-behaved citizen and put data in places the compiler writers
expected.

The next 4 bytes are the number, going from least significant byte to most
significant byte.

Finally, `0xc3` is the instruction for `ret`. `ret` fetches the return address
of the function that called our function off the stack, and jumps to it. This
transfers control back to the `main` function of the C program.

When you put all of that together, you get a very small but well-formed program
that returns `42`.

### The typedef

Next, we use C's function pointer syntax to declare a type `JitFunction` that
refers to a function that takes no arguments and returns an `int`.

```c
typedef int (*JitFunction)();
```

While technically we should specify the size of the integer (after all, we know
we want to return a 32-bit integer), I avoided that in this demo because it
adds more headers and visual noise.

This declaration, when used with the actual call to the function, tells the C
compiler how to arrange the registers and the stack for the call.

### The mmap and memcpy dance

Now we allocate a new chunk of memory. We don't use `malloc` to do it because
`mprotect` needs the address to be page-aligned. Maybe it's possible to use
`malloc` and then `posix_memalign`, but I've never seen anybody do that. So we
`mmap` it.

I don't want to explain all the possible parameter configurations for `mmap`,
especially because they vary between systems. Our configuration requests:

* memory without specifying a destination address (`addr=NULL`),
* of a particular length (`length=kProgramSize`),
* that is both readable and writable (`prot=PROT_READ | PROT_WRITE`),
* is not mapped to a file, but acts like `malloc` (`flags=MAP_ANONYMOUS`,
  `fd=-1`, `offset=0`),
* and is not shared between processes (`flags=MAP_PRIVATE`)

And, since memory is kind of useless if we don't do anything with it, we copy
the program into it.

```c
  void *memory = mmap(/*addr=*/NULL, /*length=*/kProgramSize,
                      /*prot=*/PROT_READ | PROT_WRITE,
                      /*flags=*/MAP_ANONYMOUS | MAP_PRIVATE,
                      /*filedes=*/-1, /*offset=*/0);
  memcpy(memory, program, kProgramSize);
```

You might be wondering why we need to make a whole new buffer and copy into it
if we already have some memory containing the code. There are at least two
reasons.

First, we need to guarantee that the memory is page-aligned for `mprotect` --
same as above.

Second, in our actual compiler we won't just have some static array that we
copy code from. We're going to be producing it on the fly and appending to a
buffer as we go. We'll be re-using this `mmap` dance, but not necessarily the
`memcpy`.

### The mprotect

Modern operating systems implement a [security
feature](https://en.wikipedia.org/wiki/W%5EX) called "W^X", pronounced "write
xor execute". This policy prohibits a piece of memory from being both writable
and executable at the same time, which makes it harder for people to find
exploits in buggy software.

In order to both write our program into a buffer, we need to have an explicit
transition point where our memory goes from being both readable and writable to
executable. This is `mprotect`.

```c
  int result = mprotect(memory, kProgramSize, PROT_EXEC);
  assert(result == 0 && "mprotect failed");
```

If we didn't do this, something bad would happen at runtime. On my machine, I
get a segmentation fault.

### The cast

In order to actually call the function, we need to first wrangle the `void*`
into the right type. While we could do the cast and call in one line, I find it
easier to read to cast first and call later.

```c
  JitFunction function = *(JitFunction*)&memory;
```

### The call

Ahh, some action! This very innocuous-looking code is maybe the most exciting
part of the whole program. We finally take our code, marked executable, treat
it the same as any old C function, and call it!

```c
  int return_code = function();
  assert(return_code == 42 && "the assembly was wrong");
```

The first time I got this working I was very happy with myself.

### The clean up 

Just as every `malloc` must be paired with a `free`, every `mmap` must be
paired with a `munmap`. Unlike `free`, `munmap` returns an error code so we
check it.

```c
  result = munmap(memory, kProgramSize);
  assert(result == 0 && "munmap failed");
```

### Some proof

Just so we can convince ourselves that our program actually worked (who knows,
maybe the `assert`s didn't run), propagate the result of our function call to
the outside world. We can then check the return code in `$?`.

```c
  return return_code;
```

*Note* that while the return type of `main` is `int`, return codes can only be
between 0 and 255, as they are `char`-sized.

### Wrapping up

That was a lot of words for explaining `return 42`. Hopefully they were helpful
words. With this small demo, we've gotten used to some building blocks that
we'll use when compiling and executing Lisp programs.

Next up, [compiling integers](/blog/compiling-a-lisp-2/).

{% include compiling_a_lisp_toc.md %}

[^1]: Unlike other JITs, though, we won't be doing any of the fancy inline
      caching, deoptimization, or other tricks. We're just going to compile the
      code, compile it once, and move on with our lives.

[^2]: Hold your nose and ignore the ugly pointer casting. This avoids the
      compiler complaining even with `-pedantic` on. It's technically not legal
      to cast between data pointers and function pointers, but POSIX systems
      are required to support it. *Also* relevant are the C strict aliasing
      rules, so we use `-fno-strict-aliasing`. I'm not an expert on what that
      means so see [this nice StackOverflow post](https://stackoverflow.com/questions/98650/what-is-the-strict-aliasing-rule).

[^3]: ```c
      int main() {
        return 42;
      }
      ```

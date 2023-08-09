---
title: "Writing a symbolizer for the Cinder JIT"
layout: post
date: 2022-11-03
description: >
  Adding more names to debug information is always helpful.
---

I work on [Cinder](https://github.com/facebookincubator/cinder), a just-in-time
(JIT) compiler built on top of CPython. If you aren't familiar with Cinder and
want to learn more, [a previous post about the
inliner](/blog/cinder-jit-inliner/) gives a decent overview of the JIT. This
post will talk about our function symbolizer, why we added it, and how it
works.

To follow along, check out [symbolizer.cpp][symbolizer.cpp]. If you notice
something amiss, please let me know! Either send me an email, post on
[~max/blog-comments](https://lists.sr.ht/~max/blog-comments), or comment on one
of the various angry internet sites this will eventually get posted to.

[symbolizer.cpp]: https://github.com/facebookincubator/cinder/blob/ab2f6b5ca5274bbdd632b658cdce7de2274bfc56/Jit/symbolizer.cpp

## Motivation

The JIT transforms Python bytecode to machine code. Along the way, we support
printing the intermediate representations (IRs) for debugging. We also support
disassembling the resulting machine code for the same reason.

The various IRs and machine code contain references to C and C++ functions by
address. While a running process only needs the address to go about its job,
software engineers like me need a little more than `0x3A28213A` to debug
things. This leaves us wanting a function that can go from address to function
name: a *symbolizer*.

You might wonder why we don't instead keep all of the names inside the
instructions. After all, we probably add the function pointers by name (like
`env.emit<CallCFunction>(PyNumber_Add, ...)`. Why not also add the string
`"PyNumber_Add"` alongside it?

I quite honestly do not have a good answer. I think it would take work to
thread all of that additional information through the system so that we can
guarantee it, but:

1. I already did this for the instructions that read from and write to fields.
   It's great.
2. Writing this symbolizer also took a lot of work.

In the end I decided to do what other projects like HHVM seem to do and wrote
the darn symbolizer.

## The journey

I wanted names in our debug output. Seeing stuff like
`CallCFunction<0x6339392C> v1 v7` was driving me batty.[^addresses] How am I
supposed to know what that represents? Sure, I can kind of make an inference
from the context, but it's not pleasant.

[^addresses]: Actually, it's worse than that. We didn't even print the
    addresses originally.

And it's even worse in the machine code, where there are no names. Take a look
at an example dump of assembly code from the JIT:

```nasm
Epilogue
  mov    0x118(%rdi),%rsi
  btq    $0x0,0x8(%rsi)
  mov    (%rsi),%rsi
  mov    %rsi,0x118(%rdi)
  jae    0x7f19fdf1d2f3
  mov    %rax,-0x8(%rbp)
  callq  *0x69(%rip)        # 0x7f19fdf1d358
  mov    -0x8(%rbp),%rax
```

We can see that the disassembler has helpfully annotated the RIP-relative call
with the address it found later in the instruction stream. But that number is
still meaningless to me. I would much rather have the following:

```nasm
Epilogue
  mov    0x118(%rdi),%rsi
  btq    $0x0,0x8(%rsi)
  mov    (%rsi),%rsi
  mov    %rsi,0x118(%rdi)
  jae    0x7f19fdf1d2f3
  mov    %rax,-0x8(%rbp)
  callq  *0x69(%rip)        # 0x7f19fdf1d358 (JITRT_UnlinkFrame(_ts*))
  mov    -0x8(%rbp),%rax
```

Beautiful. A crisp, clear function name. So how do we get there?

For some cases in the project we already used `dladdr` as a limited symbolizer.
Unfortunately, `dladdr` only works if the function is in some `.so` that your
application loaded. If you are trying to symbolize a function from your own
executable, you're out of luck.

I learned somewhere that at least for ELF binaries (and probably other
executable formats), there are names stored in the header. I had no idea how to
read my own ELF header. I tried to read from the start of the executable and
found an ELF header! It was great! And then I tried to read a section header
and got a segfault.

I learned (from [Employed Russian][employed-russian], as apparently
everybody who works on low-level things does) that section headers are not
loaded into memory at process start. Bummer. So how do we read the header?

[employed-russian]: https://stackoverflow.com/users/50617/employed-russian

Well, we loaded the executable from the disk on process boot. Why not read it
again? I went off to `mmap` the file `/proc/self/exe` so that I could read from
that instead.

I had some crashes, so I went to see if Valgrind could track down anything
weird for me. It turns out, though, that Valgrind [had a bug][valgrind-bug]
where it wouldn't intercept the `open` of `/proc/self/exe` for the `mmap`, so
actually I was reading *Valgrind's* executable instead of my own when trying to
track down my memory error. Talk about multiple levels of confusion. At the
time of symbolizer writing, the bug had been fixed, but I did not have the
latest version on hand.

[valgrind-bug]: https://bugzilla.redhat.com/show_bug.cgi?id=1925786

I finally got the constructor working:

```c++
Symbolizer::Symbolizer(const char* exe_path) {
  int exe_fd = ::open(exe_path, O_RDONLY);
  if (exe_fd == -1) {
    JIT_LOG("Could not open %s: %s", exe_path, ::strerror(errno));
    return;
  }
  // Close the file descriptor. We don't need to keep it around for the mapping
  // to be valid and if we leave it lying around then some CPython tests fail
  // because they rely on specific file descriptor numbers.
  SCOPE_EXIT(::close(exe_fd));
  struct stat statbuf;
  int stat_result = ::fstat(exe_fd, &statbuf);
  if (stat_result == -1) {
    JIT_LOG("Could not stat %s: %s", exe_path, ::strerror(errno));
    return;
  }
  off_t exe_size_signed = statbuf.st_size;
  JIT_CHECK(exe_size_signed >= 0, "exe size should not be negative");
  exe_size_ = static_cast<size_t>(exe_size_signed);
  exe_ = reinterpret_cast<char*>(
      ::mmap(nullptr, exe_size_, PROT_READ, MAP_PRIVATE, exe_fd, 0));
  if (exe_ == reinterpret_cast<char*>(MAP_FAILED)) {
    JIT_LOG("could not mmap");
    exe_ = nullptr;
    return;
  }
  auto elf = reinterpret_cast<ElfW(Ehdr)*>(exe_);
  auto shdr = reinterpret_cast<ElfW(Shdr)*>(exe_ + elf->e_shoff);
  const char* str = exe_ + shdr[elf->e_shstrndx].sh_offset;
  for (int i = 0; i < elf->e_shnum; i++) {
    if (shdr[i].sh_size) {
      if (std::strcmp(&str[shdr[i].sh_name], ".symtab") == 0) {
        symtab_ = reinterpret_cast<ElfW(Shdr)*>(&shdr[i]);
      } else if (std::strcmp(&str[shdr[i].sh_name], ".strtab") == 0) {
        strtab_ = reinterpret_cast<ElfW(Shdr)*>(&shdr[i]);
      }
    }
  }
  // ...
}
```

In this blob of constructor code, we:

1. `open` the file to get a file descriptor
2. `fstat` the file to get its size
3. `mmap` the file so we can read from its contents
4. Read the section headers one by one until we find `.symtab` and `.strtab`

Through a bunch of trial and error and reading too much half-working code on
the internet and too many manual pages, I got the symbolizer working! I managed
to make it symbolize function names from our executable and fall back to
`dladdr` for symbols shared objects.

```c++
std::optional<std::string_view> Symbolizer::symbolize(const void* func) {
  // Try the cache first. We might have looked it up before.
  auto cached = cache_.find(func);
  if (cached != cache_.end()) {
    return cached->second;
  }
  // Then try dladdr. It might be able to find the symbol.
  Dl_info info;
  if (::dladdr(func, &info) != 0 && info.dli_sname != nullptr) {
    return cache(func, info.dli_sname);
  }
  if (!isInitialized()) {
    return std::nullopt;
  }
  // Fall back to reading our own ELF header.
  auto sym = reinterpret_cast<ElfW(Sym)*>(exe_ + symtab_->sh_offset);
  const char* str = exe_ + strtab_->sh_offset;
  for (size_t i = 0; i < symtab_->sh_size / sizeof(ElfW(Sym)); i++) {
    if (reinterpret_cast<void*>(sym[i].st_value) == func) {
      return cache(func, str + sym[i].st_name);
    }
  }
  // ...
}
```

In this snippet, we:

1. Try reading from our cache. Nothing fancy, just an `unordered_map`
2. Try calling `dladdr`
3. Loop over the symbol table until we find a symbol whose address corresponds
   to the address passed in

Problem solved, right? Nope:

* We also ship the JIT as a `.so`. If we reference a private symbol from the
  Cinder `.so`, our fancy symbol table walker won't be able to find it because
  it only reads from the executable. `dladdr` won't be able to resolve it
  either.
* Some other reason that I can't remember right now but it was irritating.

I borrowed some of our tech lead Matt Page's code for reading `.so`s and that
solved those problems. I'm not super sure why this code is different from the
code for reading the executable ELF header. It *looks* very similar. Maybe they
can be combined. If you're going to borrow some of our code, his looks more
correct. It handles more edge cases.

Then, finally, since we're using C++, we get fun mangled names. I used
`abi::__cxa_demangle` to get a nice readable name.

## Requirements

This symbolizer only supports Linux/ELF. It won't work on macOS, which uses
Mach-O. I have no idea about BSDs and friends. It *should* be 32-bit compatible
out of the box, though, due to use of `ELfW` instead of its explicitly-sized
variants.

## Conclusion

A symbolizer that has to support very few platforms can be written in a couple
hundred lines and understood. Hopefully it's reusable. Let me know what weird
bugs you run into if you use it.

This isn't meant to be fast. I have no idea how very slow it is. Please don't
tell me. I added a cache so I wouldn't have to think about it too hard. The
thought is that once a symbol is looked up, it will likely be looked up again
in dumps of a future compiler pass.

It's going to be a minute before I go spelunking through ELF again.

## Similar work

Google's Abseil library includes [a symbolizer][abseil-sym]. Same with the
folly library: [symbolizer][folly-sym] and [elf utils][folly-elf].

[abseil-sym]: https://github.com/abseil/abseil-cpp/blob/d819278ab70ee5e59fa91d76a66abeaa106b95c9/absl/debugging/symbolize_elf.inc
[folly-sym]: https://github.com/facebook/folly/blob/74d381aacc02cfd892d394205f1e066c76e18e60/folly/experimental/symbolizer/Symbolizer.cpp
[folly-elf]: https://github.com/facebook/folly/blob/74d381aacc02cfd892d394205f1e066c76e18e60/folly/experimental/symbolizer/Elf.cpp

Also, I did a lot of reading through ClickHouse's [symbol
indexer][clickhouse-sym] and it helped clear up some misconceptions and
outright errors from various StackOverflow answers.

[clickhouse-sym]: https://github.com/ClickHouse/ClickHouse/blob/c8068bdfa260ccf486c2d0417b1eea9cbfb0ad59/src/Common/SymbolIndex.cpp

HHVM has [a symbolizer][hhvm-sym] that relies on Folly but apparently they also
support using LibBFD.

[hhvm-sym]: https://github.com/facebook/hhvm/blob/a2e15b83bd3ff360068dcf584264a42d85fe0c90/hphp/util/stack-trace.cpp

Last, I heard that you can use `libbacktrace` as a sort of symbolizer, but you
need to link with `-rdynamic` and it won't find static/private symbols.

## Resources

[ELF from scratch][efs] by Conrad Kleinespel helped me understand how the
headers are laid out. Diagrams are nice and all but it's super helpful to see
sample code iterating over section headers and stuff.

[efs]: https://www.conradk.com/2017/05/28/elf-from-scratch

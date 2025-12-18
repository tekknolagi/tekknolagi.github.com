---
title: "How to annotate JITed code for perf/samply"
layout: post
---

Brief one today. I got asked "does YJIT/ZJIT have support for [Linux] perf?"

The answer is yes, and it also works with [samply][], and the way we do it uses
the [perf map interface][].

[samply]: https://github.com/mstange/samply

[perf map interface]: https://github.com/torvalds/linux/blob/516471569089749163be24b973ea928b56ac20d9/tools/perf/Documentation/jit-interface.txt

This is the entirety of the implementation in ZJIT[^hex]:

[^hex]: We actually use `{:#x}`, which I noticed today is wrong. `{:#x}` leaves
    in the `0x`, and it shouldn't; instead **use `{:x}`**.

```rust
fn register_with_perf(iseq_name: String, start_ptr: usize, code_size: usize) {
    use std::io::Write;
    let perf_map = format!("/tmp/perf-{}.map", std::process::id());
    let Ok(file) = std::fs::OpenOptions::new().create(true).append(true).open(&perf_map) else {
        debug!("Failed to open perf map file: {perf_map}");
        return;
    };
    let mut file = std::io::BufWriter::new(file);
    let Ok(_) = writeln!(file, "{start_ptr:x} {code_size:x} zjit::{iseq_name}") else {
        debug!("Failed to write {iseq_name} to perf map file: {perf_map}");
        return;
    };
}
```

Whenever you generate a function, append a one-line entry consisting of

```
START SIZE symbolname
```

to `/tmp/perf-{PID}.map`. Per the Linux docs linked above,

> START and SIZE are hex numbers without 0x.
>
> symbolname is the rest of the line, so it could contain special characters.

## There is also the JIT dump interface

Perf map is the older way to interact with perf: a newer, more complicated way
involves [generating a "dump" file][perf-dump] and then `perf inject`ing it.

[perf-dump]: https://theunixzoo.co.uk/blog/2025-09-14-linux-perf-jit.html

<!--

## There is also the JIT gdb interface

This is not strictly related but I want to figure it out

-->

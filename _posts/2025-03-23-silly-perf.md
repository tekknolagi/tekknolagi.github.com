---
title: Optimizing Django by not being silly
layout: post
---

I just saw [this post][ravendb] and it reminded me of a time when we had a
similar situation, but with string operations in our VM. The project is now
defunct but the code is open. Let's go back in time.

[ravendb]: https://ayende.com/blog/202147-A/optimizing-by-170-000-by-not-being-silly

It was 2018 and I had just joined a team that was bringing up a new Python
runtime (then nicknamed [Pyro][pyro], now called [Skybison][skybison]). The
goal was to use the last 30 years of VM engineering research and folk knowledge
to re-design everything from scratch for performance. Also, importantly, we
were going to only have one way to encode strings: UTF-8[^surrogates] (this is
important for later).

We started from a tiny C++ core that could only run bytecode produced by the
CPython runtime's compiler. When I joined, the focus was on adding new
features. At the time, the big project was getting `importlib` working. We
needed some string functions such as [str.rpartition][str-rpartition], so I
wrote a quick and dirty implementation. This helped get
[`find_spec`][find_spec] and a bunch of other functions working.

[^surrogates]: Well, UTF-8 with surrogate pairs. But not UCS-1, UCS-2, or
    UCS-4. Bringing up a new codecs implementation led to one guy on the team
    being known as the UTF-8 guy. Sometimes UTF-8 guy would accidentally find
    heap buffer overflows in CPython while trying to understand how their CJK
    implementation worked.

[pyro]: https://pyfound.blogspot.com/2021/05/the-2021-python-language-summit-cpython.html
[skybison]: https://github.com/tekknolagi/skybison
[str-rpartition]: https://docs.python.org/3/library/stdtypes.html#str.rpartition
[find_spec]: https://github.com/python/cpython/blob/5fc889ffbfd271c651f563ab0afe2d345bacbca5/Lib/importlib/util.py#L88

Eventually, we got `importlib`, then we started work on ["Django
minimal"][django-minimal], then we started benchmarking ["Django
workload"][django-workload]. This was around a year later: October 2019.

[django-minimal]: https://github.com/tekknolagi/skybison/blob/08f3f441eef002602de86641c443287e0b994711/.github/workflows/cmake.yml#L63-L70
[django-workload]: https://github.com/facebookarchive/django-workload

It's a really cool feeling watching a project grow up. What used to shell out
to CPython to compile bytecode from inside the runtime (*wild* thing to do in
practice, but it worked well for bringup) now had its own bytecode compiler and
could run a webserver and was being profiled and optimized for performance.

Unfortunately, in order to be a fast Python runtime, you have to run Python
code quickly. And we soon hit the weirdest bottleneck in Django URL parsing. It
showed up as something ludicrous like `strIndex` taking 90% of total run-time
for a given request.

Now, let me refresh your memory about UTF-8: indexing into an arbitrary string
is an O(N) operation because the codepoints (characters, roughly) are variable
length. You need to start from the beginning and count up until you hit the
expected index. For runtimes that use UTF-8, you want to avoid loops that look
like this:

```python
for i in range(len(s)):
    s[i]  # each index operation is O(N)!
```

and instead do something like this:

```python
for c in s:
    c  # we already have the character from str.__iter__, which is fast
```

Our C++ code to do this looked something like this. The core bit is that
`str.__getitem__` calls `offsetByCodePoints` to turn the index (in
codepoint-space) to an offset (in byte-space). This in turn calls `offset`
which has to loop through the encoded string, jumping `numChars` bytes
(depending on how wide each codepoint is).

```c++
bool METH(str, __getitem___intrinsic)(Thread* thread) {
  // ...
  if (0 <= idx && idx < len) {
    word offset = self.offsetByCodePoints(0, idx);
    if (offset < len) {
      // .. fetch the code point and put it on the stack ...
      return true;
    }
  }
  // ...
}

word RawDataArray::offsetByCodePoints(word index, word count) const {
  const byte* data = dataArrayData(*this);
  return offset(data, length(), index, count);
}

static inline word offset(const byte* data, word len, word index, word count) {
  if (count >= 0) {
    while (count-- && index < len) {
      index += UTF8::numChars(data[index]);
    }
    return Utils::minimum(index, len);
  }
  while (count < 0) {
    index--;
    if (index < 0) return -1;
    if (UTF8::isLeadByte(data[index])) count++;
  }
  return index;
}
```

Now, keeping all of that that in mind, let's look at my implementation of
`str.rpartition`, the hackily implemented function from earlier (full commit
[here][rpartition-commit]):

[rpartition-commit]: https://github.com/tekknolagi/skybison/commit/15c0e6b2b11e9aed4ca58d73b3bc1857d40d1265

```python
class str:
    # ...
    def rpartition(self, sep):
        # TODO(T37438017): Write in C++
        before, itself, after = self[::-1].partition(sep[::-1])[::-1]
        return before[::-1], itself[::-1], after[::-1]
```

Haha. Oh. Oof. No. Want to count all the ways this is horrible? `rpartition`
should be able to instead iterate backwards through the string *once* (fast!),
splitting precisely twice. Instead, this:

* reverses strings (allocating a bunch of short-lived new strings)
* makes a bunch of method calls
* reverses strings *again*

I rewrote the function to do the right thing, which was to have the surface
implementation bits in Python (argument parsing, type checking) and then shell
out to our fast core string utility:

```python
class str:
    # ...
    def rpartition(self, sep):
        _str_guard(self)
        _str_guard(sep)
        return _str_rpartition(self, sep)
```

Of course, we didn't have this fast core string utility yet, so I had to
implement it. See [the rewrite commit][rpartition-rewrite-commit] for some fun
C++.

[rpartition-rewrite-commit]: https://github.com/tekknolagi/skybison/commit/17748e5cd2d5fac78bb87bec3e946c7073a37366

I don't have benchmark numbers for you because this was six years ago and the
project was still secret back then. All I remember was that `strIndex`
completely disappeared from the profiles. You can trace some of the other fun
performance work by paging through the commits around this time.

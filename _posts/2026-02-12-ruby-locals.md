---
title: "Ruby locals, just-in-time"
layout: post
---

In this post I'm going to show you the tricky details of local variables in
Ruby by showing you how we implement them in our Ruby JIT, ZJIT. That may sound
counter-intuitive---how could adding another layer of complexity on top help
explain something? Well, partially it's a form of therapy for me. But also,
sometimes adding a second implementation can help illuminate the details of the
first one. It's how I learned about CPython and now CRuby.

Let's start with the interpreter. The interpreter operates on *interpreter
frames*. These are heap-allocated blobs of memory in one contiguous region that
belongs to the CRuby runtime. It acts very much like the native C stack in that
frames are pushed and popped on function entry and exit, respectively.

## The interpreter

The interpreter has `setlocal` and `getlocal` instructions. These are
responsible for writing to and reading from the interpreter frame.

```ruby
def foo
  a = 1
  b = 2
  a + b
end
```

If we disassemble this Ruby code by doing `ruby --dump=insns_without_opt
file.rb`, we see the bytecode both for the main (top level file) code and also
for the `foo` method:

```
== disasm: #<ISeq:foo@-:1 (1,0)-(5,3)>
local table (size: 2, argc: 0 [...])
[ 2] a@0        [ 1] b@1
0000 putobject              1                         (   2)[LiCa]
0002 setlocal               a@0, 0
0005 putobject              2                         (   3)[Li]
0007 setlocal               b@1, 0
0010 getlocal               a@0, 0                    (   4)[Li]
0013 getlocal               b@1, 0
0016 send                   <calldata!mid:+, argc:1, ARGS_SIMPLE>, nil
0019 leave                                            (   5)[Re]
```

The disassembly (which I have very slightly slimmed to make fit into the page)
has some metadata about the local variables, parameters, and frame layout. Then
it contains the bytecode.

The instruction offset in bytes is the first column, then the opcode name, then
an argument to the bytecode in the third column. Ignore the stuff after that.
We'll run the bytecode from `0000` to `0019`, top to bottom.

The CRuby interpreter is a stack machine. Opcodes communicate with one another
mostly via the stack and local variables. So this instruction sequence:

1. Pushes `1` to the stack (stack: `[1]`)
1. Pops the top of the stack and stores that in `a` (slot 0: `a@0`)
   (stack: `[]`, locals: `{a: 1}`)
1. Pushes `2` to the stack (stack: `[2]`)
1. Pops the top of the stack and stores that in `b` (slot 1: `b@1`)
   (stack: `[]`, locals: `{a: 1, b: 2}`)
1. Fetches the value in local `a` and pushes it to the stack (stack: `[1]`)
1. Fetches the value in local `b` and pushes it to the stack (stack: `[1, 2]`)
1. Sends the `+` message to the item one down on the stack (`1`), passing the
   top item (`2`) as an argument.
1. Pops both off the stack and leaves the result in its place (stack: `[3]`,
   probably[^bops])
1. Pops the top item from the stack and returns from the current stack frame
   with that top element as the return value

[^bops]: It's perfectly okay to override the `+` method on `Integer` at any
    time, which means we don't actually know if the result is `3`. You could
    make it return `6` or `7` if you wanted.

<!--
TIL `for x in [1,2,3]; puts x; end` sends `#each`
-->

Most disassembly dumps won't look exactly like this though. We've disabled the
bytecode optimizer by specifying `insns_without_opt`. The bytecode optimizer
makes the interpreter faster by specializing some opcodes for common cases. For
example, instead of `1` emitting a `putobject` that looks up a value from the
constant table, there is a specialized instruction that directly pushes `1`
because it is so common. Here is the same instruction sequence but with the
bytecode optimizer turned on:

If we disassemble this Ruby code by doing `ruby --dump=insns file.rb`, we see
the bytecode both for the main (top level file) code and also for the `foo`
method:

```
== disasm: #<ISeq:foo@-:1 (1,0)-(5,3)>
local table (size: 2, argc: 0 [...])
[ 2] a@0        [ 1] b@1
0000 putobject_INT2FIX_1_                                 (   2)[LiCa]
0001 setlocal_WC_0              a@0
0003 putobject                  2                         (   3)[Li]
0005 setlocal_WC_0              b@1
0007 getlocal_WC_0              a@0                       (   4)[Li]
0009 getlocal_WC_0              b@1
0011 opt_plus                   <calldata!mid:+, argc:1, ARGS_SIMPLE>[CcCr]
0013 leave                                                (   5)[Re]
```

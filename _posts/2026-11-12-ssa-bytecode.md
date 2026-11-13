---
title: "SSA from bytecode: imperative pearl"
layout: post
---

Ufuk made a comment that he enjoyed my description of SSA from bytecode in
[Aaron's livestream with me](https://www.youtube.com/watch?v=h9_FyHYUhww).
While filming the livestream---an intro to ZJIT internals---I got a little
nervous that people would shy away from our 1900 line `add_iseq_to_hir`
function. This function is responsible for converting CRuby's stack based
bytecode for the YARV virtual machine into a high-level SSA IR for ZJIT to
analyze and optimize.

It takes bytecode that looks roughly like this:

```
push 3
push 4
add
```

into SSA form, giving an SSA value to each stack slot, ish:

```
v0 = const 3
v1 = const 4
v2 = add v0, v1
```

While our function is 2000 lines long because it handles a lot of specific
opcodes and weird facts about those opcodes, the core of it is small. Here is
that core.

## What if a stack, but virtual

The bytecode assumes that when it is run, it will be operating on a stack. This
means that every opcode has implicit stack inputs and implicit stack outputs.

`push 3`, for example, has empty stack inputs and one stack output. By
contrast, `add` has two stack inputs and one stack output. Some instructions
such as `nop` are empty on both counts. As far as I can tell, all instructions
have *at most* one output.

These counts are known when looking at the bytecode: we can reason statically
about how many inputs and outputs each opcode has. Some are variadic, such as
`send` (call), but they come bearing an argument count in a nearby field.

Let's model how the first example runs on the VM by adding a view of the stack
after each instruction has run. The stack will be displayed as a list
left-to-right, where the left is the bottom and the right is the top.

```
       ; []
push 3 ; [3]
push 4 ; [3, 4]
add    ; [7]
```

Because the input counts and output counts are static, when ZJIT reads through
the bytecode, we can run the stack parts of it at compile-time.

We don't *really* need to know what the values on the stack are; we only need
to keep track of where they came from. And when I say "where they came from", I
mean that we name each instruction and refer to its output by the instruction's
name. I like how the paper [Design of the Java HotSpot&trade; Client Compiler for
Java 6](/assets/img/design-hotspot-client-compiler.pdf) (PDF) puts it:

> An instruction that loads or computes a value represents both the operation
> and its result, so that operands can be represented as pointers to previous
> instructions.

Here is what that looks like for the above example. In addition to the run-time
stack, I will also write the SSA instruction to indicate that we are generating
new code:

```
       ; []
push 3 ; v0 = const 3
       ; [v0]
push 4 ; v1 = const 4
       ; [v0, v1]
add    ; v2 = add v0, v1
       ; [v2]
```

This is neat! We have transformed stack based bytecode to a form where
instructions can directly reference their inputs. You may notice, though, that
`v0` and `v2` both end up in the same stack slot (the first element). This is
why I said earlier "giving an SSA value to each stack slot, *ish*".

The difference between the two is *time*. `v0` was in the operand stack at one
point but now `v2` is. This is kind of why I describe SSA as "pushing time
through the IR".

Alright, let's implement it.

## Implementation

We'll start with the real ("concrete") interpreter of the bytecode, which
operates on a value stack. This gives you a simplified version of how Ruby
internals work:

```ruby
def interpret(bytecode)
  stack = []
  bytecode.each do |op|
    case op
    in [:push, c]
      stack << c
    in [:add]
      right = stack.pop
      left = stack.pop
      stack << left + right
    else
      raise "Unknown op #{op}"
    end
  end
  stack.pop
end

bytecode = [
  [:push, 3],
  [:push, 4],
  [:add],
]

puts interpret(bytecode)  # 7
```

Now let's look at the abstract intepreter, which by contrast operates on an SSA
stack. You can see that they are quite similar looking:

```ruby
def to_ssa(bytecode)
  code = []
  stack = []
  bytecode.each do |op|
    case op
    in [:push, c]
      name = :"v#{code.length}"
      code << "#{name} = const #{c}"
      stack << name
    in [:add]
      name = :"v#{code.length}"
      right = stack.pop
      left = stack.pop
      code << "#{name} = add #{left}, #{right}"
      stack << name
    else
      raise "Unknown op #{op}"
    end
  end
  code
end

bytecode = [
  [:push, 3],
  [:push, 4],
  [:add],
]

puts to_ssa(bytecode)
# v0 = const 3
# v1 = const 4
# v2 = add v0, v1
```

The `name`s could be anything as long as they are unique. For simplicity and
ease of reading, I picked the length of the `code` array, which grows
monotonically. This is an array/vector-based analogue to the real-pointer
approach in the HotSpot paper from above.

Ta da! You have built a small abstract interpreter.

Now to make it global (to handle control flow) you need some kind of
inter-basic-block data flow mechanism such as phi nodes or basic block
parameters. Perhaps more on that in another post, but for now, you may enjoy
[my collection of other people's writing](/blog/ssa/) about that.

## Conclusion

I originally wanted to title this blog post "you could have invented abstract
interpretation" or similar, but I decided that I find those titles obnoxious
because they assume the reader is or thinks of themself as unintelligent.
Perhaps *we* could have invented it.

The title is a bit of a tongue-in-cheek nod to the "functional pearl" papers
that I always felt should be simpler than they are but leave me confused. And
also kind of a nicer nod to [SSA is Functional
Programming](/assets/img/appel-basic-block-arguments.pdf) (PDF) by Andrew Appel.

## References

* [A New Approach to Forth Native Code Generation](/assets/img/ertl92.pdf) (PDF, 1992)
* [Java Bytecode to Native Code Translation: The Caffeine Prototype and Preliminary Results](/assets/img/caffeine.pdf) (PDF, 1996) section 3.2
* [CACAO - A 64 bit JavaVM Just-in-Time Compiler](/assets/img/cacao-jvm.pdf) (PDF, 1997) section 3
* [Fast, Effective Code Generation in a Just-In-Time Java Compiler](https://dl.acm.org/doi/pdf/10.1145/277650.277740) (PDF, 1998) section 2.2, the mimic stack
* [The Jalapeño Dynamic Optimizing Compiler for Java&trade;](https://dl.acm.org/doi/pdf/10.1145/304065.304113) (PDF, 1999)
* [Dynamic optimization through the use of automatic runtime specialization](https://dspace.mit.edu/entities/publication/6896835b-c46b-4ebb-8ecc-1f5cbac364b5) (1999) section 3.4

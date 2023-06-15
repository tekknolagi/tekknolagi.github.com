---
title: "Bytecode compilers and interpreters"
layout: post
date: 2019-01-03 18:15:00 PST
---

Two fun things happened recently and the proximity of the two made something go
*click* in my head and now I think I understand how bytecode interpreters work.

1. I went to a class at work called "Interpreters 101" or something of the
   sort. In it, the presenter walked through creating a dead-simple
   tree-walking Lisp interpreter. Then he ended by suggesting we go out and
   re-implement it as a bytecode interpreter or even a JIT.
2. I joined a new team at work that is working on something Python related.
   Because of my new job responsibilities, I have had to become rather closely
   acquainted with CPython 3.6 bytecode.

Since learning by writing seems to be something I do frequently, here is a blog
post about writing a small bytecode compiler and interpreter in small pieces.
We'll go through it in the same manner as my [lisp
interpreter][lisp-interpreter]: start with the simplest pices and build up the
rest of the stack as we need other components.

[lisp-interpreter]: https://bernsteinbear.com/blog/lisp/


### Some definitions

Before we dive right in, I'm going to make some imprecise definitions that
should help differentiate types of interpreters:

* *Tree-walking* interpreters process the program AST node by node, recursively
   evaluating based on some evaluation rules. For a program like `Add(Lit 1,
   Lit 2)`, it might see the add rule, recursively evaluate the arguments, add
   them together, and then package that result in the appropriate value type.
* *Bytecode* interpreters don't work on the AST directly. They work on
  bytecode, which is a transformation of the AST into a more linear form. This
  simplifies the execution model and can produce impressive speed wins. A
  program like `Add(Lit 1, Lit 2)` might be bytecode compiled into the
  following bytecode:

  ```
  PUSH 1
  PUSH 2
  ADD
  ```

  And then the interpreter would go instruction by instruction, sort of like a
  hardware CPU (it's interpreters all the way down!).
* *JIT* interpreters are like bytecode interpreters except instead of compiling
  to language implementation-specific bytecode, they try to compile to native
  machine instructions. Most production-level JIT interpreters "warm up" by
  measuring what functions get called the most and then compiling them to
  native code in the background. This way, the so-called "hot code" gets
  optimized and has a smaller performance penalty.

My *Writing a Lisp* series presents a tree-walking interpreter. This much
smaller post will present a bytecode interpreter. A future post may present a
JIT compiler when I figure out how they work.

Without further ado, let us learn.


### In the beginning, there was a tree-walking interpreter

Lisp interpreters have pretty simple semantics. Below is a sample REPL
(read-eval-print-loop) where all the commands are `parse`d into ASTs and then
`eval`ed straightaway. See Peter Norvig's [lis.py][lispy] for a similar
tree-walking interpreter.

[lispy]: http://norvig.com/lispy.html

```
>>> 1
1
>>> '1'
1
>>> "hello"
hello
>>> (val x 3)
None
>>> x
3
>>> (set x 7)
None
>>> x
7
>>> (if True 3 4)
3
>>> (lambda (x) (+ x 1))
<Function instance...>
>>> ((lambda (x) (+ x 1)) 5)
6
>>> (define f (x) (+ x 1))
None
>>> f
<Function instance...>
>>> +
<function + at...>
>>> (f 10)
11
>>>
```

For our interpreter, we're going to write a function called `compile` that
takes in an expression represented by a Python list (something like `['+', 5,
['+', 3, 5]]`) and returns a list of bytecode instructions. Then we'll write
`eval` that takes in those instructions and returns a Python value (in this
case, the int `13`). It should behave identically to the tree-walking
interpreter, except faster.


### The [ISA][ISA] (Instruction Set Architecture)

For our interpreter we'll need a surprisingly small set of instructions, mostly
lifted from the CPython runtime's own instruction set architecture. CPython is
a stack-based architecture, so ours will be too.

* `LOAD_CONST`
  * Pushes constants onto the stack.
* `STORE_NAME`
  * Stores values into the environment.
* `LOAD_NAME`
  * Reads values from the environment.
* `CALL_FUNCTION`
  * Calls a function (built-in or user-defined).
* `RELATIVE_JUMP_IF_TRUE`
  * Jumps if the value on top of the stack is true.
* `RELATIVE_JUMP`
  * Jumps.
* `MAKE_FUNCTION`
  * Creates a function object from a code object on the stack and pushes it on
    the stack.

With these instructions we can define an entire language. Most people choose to
define math operations in their instruction sets for speed, but we'll define
them as built-in functions because it's quick and easy.

[ISA]: https://en.wikipedia.org/wiki/Instruction_set_architecture

### The `Opcode` and `Instruction` classes

I've written an `Opcode` enum:

```python
import enum


class AutoNumber(enum.Enum):
    def _generate_next_value_(name, start, count, last_values):
        return count


@enum.unique
class Opcode(AutoNumber):
    # Add opcodes like:
    # OP_NAME = enum.auto()
    pass
```

Its sole purpose is to enumerate all of the possible opcodes --- we'll add them
later. Python's `enum` API is pretty horrific so you can gloss over this if you
like and pretend that the opcodes are just integers.

I've also written an `Instruction` class that stores opcode and optional
argument pairs:

```python
class Instruction:
    def __init__(self, opcode, arg=None):
        self.opcode = opcode
        self.arg = arg

    def __repr__(self):
        return "<Instruction.{}({})>".format(self.opcode.name, self.arg)

    def __call__(self, arg):
        return Instruction(self.opcode, arg)

    def __eq__(self, other):
        assert(isinstance(other, Instruction))
        return self.opcode == other.opcode and self.arg == other.arg
```

The plan is to declare one `Instruction` instance per opcode, naming its
argument something sensible. Then when bytecode compiling, we can make
instances of each instruction by calling them with a given argument. This is
pretty hacky but it works alright.

```python
# Creating top-level opcodes
STORE_NAME = Instruction(Opcode.STORE_NAME, "name")
# Creating instances
[STORE_NAME("x"), STORE_NAME("y")]
```

Now that we've got some infrastructure for compilation, let's start compiling.


### Integers

This is what our `compile` function looks like right now:

```python
def compile(exp):
    raise NotImplementedError(exp)
```

It is not a useful compiler, but at least it will let us know what operations
it does not yet support. Let's make it more useful.

The simplest thing I can think of to compile is an integer. If we see one, we
should put it on the stack. That's all! So let's first add some instructions
that can do that, and then implement it.

```python
class Opcode(AutoNumber):
    LOAD_CONST = enum.auto()
```

I called it `LOAD_CONST` because that's what Python calls its opcode that does
something similar. "Load" is a sort of confusing word for what its doing,
because that doesn't specify *where* it's being loaded. If you want, you can
call this `PUSH_CONST`, which in my opinion better implies that it is to be
pushed onto the VM stack. I also specify `CONST` because this instruction
should only be used for literal values: `5`, `"hello"`, etc. Something where
the value is completely defined by the expression itself.

Here's the parallel for the `Instruction` class (defined at the module scope):

```python
LOAD_CONST = Instruction(Opcode.LOAD_CONST, "const")
```

The argument name `"const"` is there only for bytecode documentation for the
reader. It will be replaced by an actual value when this instruction is created
and executed. Next, let's add in a check to catch this case.

```python
def compile(exp):
    if isinstance(exp, int):
        return [LOAD_CONST(exp)]
    raise NotImplementedError(exp)
```

Oh, yeah. `compile` is going to walk the expression tree and merge together
lists of instructions from compiled sub-expressions --- so every branch has to
return a list. This will look better when we have nested cases. Let's test it
out:

```python
assert compile(5) == [LOAD_CONST(5)]
assert compile(7) == [LOAD_CONST(7)]
```

Now that we've got an instruction, we should also be able to run it. So let's
set up a basic `eval` loop:

```python
def eval(self, code):
    pc = 0
    while pc < len(code):
        ins = code[pc]
        op = ins.opcode
        pc += 1
        raise NotImplementedError(op)
```

Here `pc` is short for "program counter", which is equivalent to the term
"instruction pointer", if you're more familiar with that. The idea is that we
iterate through the instructions one by one, executing them in order.

This loop will throw when it cannot handle a particular instruction, so it is
reasonable scaffolding, but not much more. Let's add a case to handle
`LOAD_CONST` in `eval`.

```python
def eval(code):
    pc = 0
    stack = []
    while pc < len(code):
        ins = code[pc]
        op = ins.opcode
        pc += 1
        if op == Opcode.LOAD_CONST:
            stack.append(ins.arg)
        else:
            raise NotImplementedError(op)
    if stack:
        return stack[-1]
```

Note, since it will come in handy later, that `eval` returns the value on the
top of the stack.  This is the beginning of our calling convention, which we'll
flesh out more as the post continues. Let's see if this whole thing works, end
to end.

```python
assert eval(compile(5)) == 5
assert eval(compile(7)) == 7
```

And now let's run it:

```
willow% python3 ~/tmp/bytecode0.py
willow%
```

Swell.


### Naming things


<div style="text-align: center; padding-bottom: 20px;">
    <p style="display: inline-block; text-align: left;">
    My name, and yours, and the true name of the sun, or a spring of <br />
    water, or an unborn child, all are syllables of the great word that <br />
    is very slowly spoken by the shining of the stars. There is no other <br />
    power. No other name.
    </p>
    <br />
    <cite style='font-style: normal;'>-- Ursula K Le Guin, <i>A Wizard of Earthsea</i></cite>
</div>

Now, numbers aren't much fun if you can't do anything with them. Right now the
only valid programs are programs that push one number onto the stack. Let's
add some opcodes that put those values into variables.

We'll be adding `STORE_NAME`, which takes one value off the stack and stores it
in the current environment, and `LOAD_NAME`, which reads a value from the
current environment and pushes it onto the stack.

```python
@enum.unique
class Opcode(AutoNumber):
    LOAD_CONST = enum.auto()
    STORE_NAME = enum.auto()
    LOAD_NAME = enum.auto()

# ...

STORE_NAME = Instruction(Opcode.STORE_NAME, "name")
LOAD_NAME = Instruction(Opcode.LOAD_NAME, "name")
```

Let's talk about our representation of environments. Our `Env` class looks like
this (based on Dmitry Soshnikov's "Spy" interpreter):

```python
class Env(object):
    # table holds the variable assignments within the env. It is a dict that
    # maps names to values.
    def __init__(self, table, parent=None):
        self.table = table
        self.parent = parent

    # define() maps a name to a value in the current env.
    def define(self, name, value):
        self.table[name] = value

    # assign() maps a name to a value in whatever env that name is bound,
    # raising a ReferenceError if it is not yet bound.
    def assign(self, name, value):
        self.resolve(name).define(name, value)

    # lookup() returns the value associated with a name in whatever env it is
    # bound, raising a ReferenceError if it is not bound.
    def lookup(self, name):
        return self.resolve(name).table[name]

    # resolve() finds the env in which a name is bound and returns the whole
    # associated env object, raising a ReferenceError if it is not bound.
    def resolve(self, name):
        if name in self.table:
            return self

        if self.parent is None:
            raise ReferenceError(name)

        return self.parent.resolve(name)

    # is_defined() checks if a name is bound.
    def is_defined(self, name):
        try:
            self.resolve(name)
            return True
        except ReferenceError:
            return False
```

Our execution model will make one new `Env` per function call frame and one new
env per closure frame, but we're not quite there yet. So if that doesn't yet
make sense, ignore it for now.

What we care about right now is the global environment. We're going to make one
top-level environment for storing values. We'll then thread that through the
`eval` function so that we can use it. But let's not get ahead of ourselves.
Let's start by compiling.

```python
def compile(exp):
    if isinstance(exp, int):
        return [LOAD_CONST(exp)]
    elif isinstance(exp, list):
        assert len(exp) > 0
        if exp[0] == 'val':  # (val n v)
            assert len(exp) == 3
            _, name, subexp = exp
            return compile(subexp) + [STORE_NAME(name)]
    raise NotImplementedError(exp)
```

I've added this second branch to check if the expression is a list, since we'll
mostly be dealing with lists now. Since we also have just about zero
error-handling right now, I've also added some `assert`s to help with code
simplicity.

In the `val` case, we want to extract the name and the subexpression ---
remember, we won't just be compiling simple values like `5`; the values might
be `(+ 1 2)`. Then, we want to compile the subexpression and add a `STORE_NAME`
instruction. We can't test that just yet --- we don't have more complicated
expressions --- but we'll get there soon enough. Let's test what we can,
though:

```python
assert compile(['val', 'x', 5]) == [LOAD_CONST(5), STORE_NAME('x')]
```

Now let's move back to `eval`.

```python
def eval(code, env):
    pc = 0
    stack = []
    while pc < len(code):
        ins = code[pc]
        op = ins.opcode
        pc += 1
        if op == Opcode.LOAD_CONST:
            stack.append(ins.arg)
        elif op == Opcode.STORE_NAME:
            val = stack.pop(-1)
            env.define(ins.arg, val)
        else:
            raise NotImplementedError(ins)
    if stack:
        return stack[-1]
```

You'll notice that I've

* Added an `env` parameter, so that we can evaluate expressions in different
  contexts and get different results
* Added a case for `STORE_NAME`

We'll have to modify our other tests to pass an `env` parameter --- you can
just pass `None` if you are feeling lazy.

Let's make our first environment and test out `STORE_NAME`. For this, I'm going
to make an environment and test that storing a name in it side-effects that
environment.

```python
env = Env({}, parent=None)
eval([LOAD_CONST(5), STORE_NAME('x')], env)
assert env.table['x'] == 5
```

Now we should probably go about adding compiler and evaluator functionality for
reading those stored values. The compiler will just have to check for variable
accesses, represented just as strings.

```python
def compile(exp):
    if isinstance(exp, int):
        return [LOAD_CONST(exp)]
    elif isinstance(exp, str):
        return [LOAD_NAME(exp)]
    # ...
```

And add a test for it:

```python
assert compile('x') == [LOAD_NAME('x')]
```

Now that we can generate `LOAD_NAME`, let's add `eval` support for it. If we
did anything right, its implementation should pretty closely mirror that of its
sister instruction.

```python
def eval(code, env):
    # ...
    while pc < len(code):
        # ...
        elif op == Opcode.STORE_NAME:
            val = stack.pop(-1)
            env.define(ins.arg, val)
        elif op == Opcode.LOAD_NAME:
            val = env.lookup(ins.arg)
            stack.append(val)
        # ...
    # ...
```

To test it, we'll first manually store a name into the environment, then see if
`LOAD_NAME` can read it back out.

```python
env = Env({'x': 5}, parent=None)
assert eval([LOAD_NAME('x')], env) == 5
```

Neat.


### Built-in functions

We can add as many opcodes as features we need, or we can add one opcode that
allows us to call native (Python) code and extend our interpreter that way.
Which approach you take is mostly a matter of taste.

In our case, we'll add the `CALL_FUNCTION` opcode, which will be used both for
built-in functions and for user-defined functions. We'll get to user-defined
functions later.

`CALL_FUNCTION` will be generated when an expression is of the form `(x0 x1 x2
...)` and `x0` is not one of the pre-set recognized names like `val`. The
compiler should generate code to load first the function, then the arguments
onto the stack. Then it should issue the `CALL_FUNCTION` instruction. This is
very similar to CPython's implementation.

I'm not going to reproduce the `Opcode` declaration, because all of those look
the same, but here is my `Instruction` declaration:

```python
CALL_FUNCTION = Instruction(Opcode.CALL_FUNCTION, "nargs")
```

The `CALL_FUNCTION` instruction takes with it the number of arguments passed to
the function so that we can call it correctly. Note that we do this instead of
storing the correct number of arguments in the function because functions could
take variable numbers of arguments.

Let's compile some call expressions.

```python
def compile(exp):
    # ...
    elif isinstance(exp, list):
        assert len(exp) > 0
        if exp[0] == 'val':
            assert len(exp) == 3
            _, name, subexp = exp
            return compile(subexp) + [STORE_NAME(name)]
        else:
            args = exp[1:]
            nargs = len(args)
            arg_code = sum([compile(arg) for arg in args], [])
            return compile(exp[0]) + arg_code + [CALL_FUNCTION(nargs)]
    # ...
```

I've added a default case for list expressions. See that it compiles the name,
then the arguments, then issues a `CALL_FUNCTION`. Let's test it out with 0, 1,
and more arguments.

```python
assert compile(['hello']) == [LOAD_NAME('hello'), CALL_FUNCTION(0)]
assert compile(['hello', 1]) == [LOAD_NAME('hello'), LOAD_CONST(1),
                                    CALL_FUNCTION(1)]
assert compile(['hello', 1, 2]) == [LOAD_NAME('hello'), LOAD_CONST(1),
                                    LOAD_CONST(2), CALL_FUNCTION(2)]
```

Now let's implement `eval`.

```python
def eval(code, env):
    # ...
    while pc < len(code):
        # ...
        elif op == Opcode.CALL_FUNCTION:
            nargs = ins.arg
            args = [stack.pop(-1) for i in range(nargs)][::-1]
            fn = stack.pop(-1)
            assert callable(fn)
            stack.append(fn(args))
        else:
            raise NotImplementedError(ins)
    # ...
```

Notice that we're reading the arguments off the stack --- in reverse order ---
and then reading the function off the stack. Everything is read the opposite
way it is pushed onto the stack, since, you know, it's a stack.

We also check that the `fn` is callable. This is a Python-ism. Since we're
allowing raw Python objects on the stack, we have to make sure that we're
actually about to call a Python function. Then we'll call that function with a
list of arguments and push its result on the stack.

Here's what this looks like in real life, in a test:

```python
env = Env({'+': lambda args: args[0] + args[1]}, None)
assert eval(compile(['+', 1, 2]), env) == 3
```

This is pretty neat. If we stuff `lambda` expressions into environments, we get
these super easy built-in functions. But that's not quite the most optimal `+`
function. Let's make a variadic one for fun.

```python
env = Env({'+': sum}, None)
assert eval(compile(['+', 1, 2, 3, 4, 5]), env) == 15
assert eval(compile(['+']), env) == 0
```

Since we pass the arguments a list, we can do all sorts of whack stuff like
this!

Since I only alluded to it earlier, let's add a test for compiling nested
expressions in `STORE_NAME`.

```python
env = Env({'+': sum}, None)
eval(compile(['val', 'x', ['+', 1, 2, 3]]), env)
assert env.table['x'] == 6
```

Go ahead and add all the builtin functions that your heart desires... like
`print`!

```python
env = Env({'print': print}, None)
eval(compile(['print', 1, 2, 3]), env)
```

You should see the arguments passed in the correct order. Note that if you are
using Python 2, you should wrap the print in a `lambda`, since `print` is not a
function.


### Conditionals

Without conditionals, we can't really do much with our language. While we could
choose to implement conditionals eagerly as built-in functions, we're going to
do "normal" conditionals. Conditionals that lazily evaluate their branches.
This can't be done with our current execution model because all arguments are
evaluated before being passed to built-in functions.

We're going to do conditionals the traditional way:

```scheme
(if a b c)
```

will compile to

```
[a BYTECODE]
RELATIVE_JUMP_IF_TRUE b
[c BYTECODE]
RELATIVE_JUMP end
b:
[b BYTECODE]
end:
```

We're also going to take the CPython approach in generating *relative* jumps
instead of absolute jumps. This way we don't need a separate target resolution
step.

To accomplish this, we'll add two the opcodes and instructions listed above:

```python
RELATIVE_JUMP_IF_TRUE = Instruction(Opcode.RELATIVE_JUMP_IF_TRUE, "off")
RELATIVE_JUMP = Instruction(Opcode.RELATIVE_JUMP, "off")
```

Each of them takes an offset in instructions of how far to jump. This could be
positive or negative --- for loops, perhaps --- but right now we will only
generate positive offsets.

We'll add one new case in the compiler:

```python
def compile(exp):
    # ...
    elif isinstance(exp, list):
        # ...
        elif exp[0] == 'if':
            assert len(exp) == 4
            _, cond, iftrue, iffalse = exp
            iftrue_code = compile(iftrue)
            iffalse_code = compile(iffalse) + [RELATIVE_JUMP(len(iftrue_code))]
            return (compile(cond)
                    + [RELATIVE_JUMP_IF_TRUE(len(iffalse_code))]
                    + iffalse_code
                    + iftrue_code)
        # ...
```

First compile the condition. Then, compile the branch that will execute if the
condition passes. The if-false branch is a little bit tricky because I am also
including the jump-to-end in there. This is so that the offset calculation for
the jump to the if-true branch is correct (I need not add `+1`).

Let's add some tests to check our work:

```python
assert compile(['if', 1, 2, 3]) == [LOAD_CONST(1), RELATIVE_JUMP_IF_TRUE(2),
                                    LOAD_CONST(3), RELATIVE_JUMP(1),
                                    LOAD_CONST(2)]
assert compile(['if', 1, ['+', 1, 2], ['+', 3, 4]]) == \
        [LOAD_CONST(1),
         RELATIVE_JUMP_IF_TRUE(5),
         LOAD_NAME('+'), LOAD_CONST(3), LOAD_CONST(4), CALL_FUNCTION(2),
         RELATIVE_JUMP(4),
         LOAD_NAME('+'), LOAD_CONST(1), LOAD_CONST(2), CALL_FUNCTION(2)]
```

I added the second test to double-check that nested expressions work correctly.
Looks like they do. On to `eval`!

This part should be pretty simple --- adjust the `pc`, sometimes conditionally.

```python
def eval(code, env):
    # ...
    while pc < len(code):
        # ...
        elif op == Opcode.RELATIVE_JUMP_IF_TRUE:
            cond = stack.pop(-1)
            if cond:
                pc += ins.arg  # pc has already been incremented
        elif op == Opcode.RELATIVE_JUMP:
            pc += ins.arg  # pc has already been incremented
        # ...
    # ...
```

If it takes a second to convince yourself that this is not off-by-one, that
makes sense. Took me a little bit too. And hey, if convincing yourself isn't
good enough, here are some tests.

```python
assert eval(compile(['if', 'true', 2, 3]), Env({'true': True})) == 2
assert eval(compile(['if', 'false', 2, 3]), Env({'false': False})) == 3
assert eval(compile(['if', 1, ['+', 1, 2], ['+', 3, 4]]), Env({'+': sum})) == 3
```


### Defining your own functions

User-defined functions are absolutely key to having a usable programming
language. Let's let our users do that. Again, we're using Dmitry's `Function`
representation, which is wonderfully simple.

```python
class Function(object):
    def __init__(self, params, body, env):
        self.params = params
        self.body = body
        self.env = env # closure!
```

The params will be a tuple of names, the body a tuple of instructions, and the
env an `Env`.

In our language, all functions will be closures. They can reference variables
defined in the scope where the function is defined (and above). We'll use the
following forms:

```scheme
((lambda (x) (+ x 1)) 5)
; or
(define inc (x) (+ x 1))
(inc 5)
```

In fact, we're going to use a syntax transformation to re-write `define`s in
terms of `val` and `lambda`. This isn't required, but it's kind of neat.

For this whole thing to work, we're going to need a new opcode:
`MAKE_FUNCTION`. This will convert some objects stored on the stack into a
`Function` object.

```python
MAKE_FUNCTION = Instruction(Opcode.MAKE_FUNCTION, "nargs")
```

This takes an integer, the number of arguments that the function expects. Right
now we only allow positional, non-optional arguments. If we wanted to have
additional calling conventions, we'd have to add them later.

Let's take a look at `compile`.

```python
def compile(exp):
    # ...
    elif isinstance(exp, list):
        assert len(exp) > 0
        # ...
        elif exp[0] == 'lambda':
            assert len(exp) == 3
            (_, params, body) = exp
            return [LOAD_CONST(tuple(params)),
                    LOAD_CONST(tuple(compile(body))),
                    MAKE_FUNCTION(len(params))]
        elif exp[0] == 'define':
            assert len(exp) == 4
            (_, name, params, body) = exp
            return compile(['lambda', params, body]) + [STORE_NAME(name)]
    # ...
```

For `lambda`, it's pretty straightforward. Push the params, push the body code,
make a function.

`define` is a little sneaker. It acts as a macro and *rewrites the AST* before
compiling it. If we wanted to be more professional, we could make a macro
system so that the standard library could define `define` and `if`... but
that's too much for right now. But still. It's pretty neat.

Before we move on to `eval`, let's quickly check our work.

```python
assert compile(['lambda', ['x'], ['+', 'x', 1]]) == \
    [LOAD_CONST(('x',)),
     LOAD_CONST((
         LOAD_NAME('+'),
         LOAD_NAME('x'),
         LOAD_CONST(1),
         CALL_FUNCTION(2))),
     MAKE_FUNCTION(1)]
assert compile(['define', 'f', ['x'], ['+', 'x', 1]]) == \
    [LOAD_CONST(('x',)),
     LOAD_CONST((
         LOAD_NAME('+'),
         LOAD_NAME('x'),
         LOAD_CONST(1),
         CALL_FUNCTION(2))),
     MAKE_FUNCTION(1),
     STORE_NAME('f')]
```

Alright alright alright. Let's get these functions created. We need to handle
the `MAKE_FUNCTION` opcode in `eval`.

```python
def eval(code, env):
    # ...
    while pc < len(code):
        # ...
        elif op == Opcode.MAKE_FUNCTION:
            nargs = ins.arg
            body_code = stack.pop(-1)
            params = stack.pop(-1)
            assert len(params) == nargs
            stack.append(Function(params, body_code, env))
        # ...
    # ...
```

As with calling functions, we read everything in the reverse of the order that
we pushed it. First the body, then the params --- checking that they're the
right length --- then push the new `Function` object.

But `Function`s aren't particularly useful without being callable. There are
two strategies for calling functions, one slightly slicker than the other. You
can choose which you like.

The first strategy, the simple one, is to add another case in `CALL_FUNCTION`
that handles `Function` objects. This is what most people do in most
programming languages. It looks like this:

```python
def eval(code, env):
    # ...
    while pc < len(code):
        # ...
        elif op == Opcode.CALL_FUNCTION:
            nargs = ins.arg
            args = [stack.pop(-1) for i in range(nargs)][::-1]
            fn = stack.pop(-1)
            if callable(fn):
                stack.append(fn(args))
            elif isinstance(fn, Function):
                actuals_record = dict(zip(fn.params, args))
                body_env = Env(actuals_record, fn.env)
                stack.append(eval(fn.body, body_env))
            else:
                raise RuntimeError("Cannot call {}".format(fn))
        # ...
    # ...
```

Notice that the function environment consists solely of the given arguments and
its parent is the stored environment --- *not* the current one.

The other approach is more Pythonic, I think. It turns `Function` into a
callable object, putting the custom setup code into `Function` itself. If you
opt to do this, leave `CALL_FUNCTION` alone and modify `Function` this way:

```python
class Function(object):
    def __init__(self, params, body, env):
        self.params = params
        self.body = body
        self.env = env # closure!

    def __call__(self, actuals):
        actuals_record = dict(zip(self.params, actuals))
        body_env = Env(actuals_record, self.env)
        return eval(self.body, body_env)
```

Then `eval` should call the `Function` as if it were a normal Python function.
Cool... or gross, depending on what programming languages you are used to
working with.

They should both work as follows:

```python
env = Env({'+': sum}, None)
assert eval(compile([['lambda', 'x', ['+', 'x', 1]], 5]), env) == 6

eval(compile(['define', 'f', ['x'], ['+', 'x', 1]]), env)
assert isinstance(env.table['f'], Function)
```

Hell, even recursion works! Let's write ourselves a little factorial function.

```python
import operator
env = Env({
    '*': lambda args: args[0] * args[1],
    '-': lambda args: args[0] - args[1],
    'eq': lambda args: operator.eq(*args),
}, None)
eval(compile(['define', 'factorial', ['x'],
                ['if',
                ['eq', 'x', 0],
                1,
                ['*', 'x', ['factorial', ['-', 'x', 1]]]]]), env)
assert eval(compile(['factorial', 5]), env) == 120
```

Which works, but it feels like we're lacking the ability to sequence
operations... because we are! So let's add that.


### One teensy little thing is missing

It should suffice to write a function `compile_program` that can take a list of
expressions, compile them, and join them. This alone is not enough, though. We
should expose that to the user so that they can sequence operations when they
need to. So let's also add a `begin` keyword.

```python
def compile_program(prog):
    return [instr for exp in prog for instr in compile(exp)]  # flatten
```

And then a case in `compile`:

```python
def compile(exp):
    # ...
    elif isinstance(exp, list):
        # ...
        elif exp[0] == 'begin':
            return compile_program(exp[1:])
        else:
            args = exp[1:]
            nargs = len(args)
            arg_code = sum([compile(arg) for arg in args], [])
            return compile(exp[0]) + arg_code + [CALL_FUNCTION(nargs)]
    raise NotImplementedError(exp)
```

And of course a test:

```python
import operator
env = Env({
    '*': lambda args: args[0] * args[1],
    '-': lambda args: args[0] - args[1],
    'eq': lambda args: operator.eq(*args),
}, None)
assert eval(compile(['begin',
    ['define', 'factorial', ['x'],
        ['if',
        ['eq', 'x', 0],
        1,
        ['*', 'x', ['factorial', ['-', 'x', 1]]]]],
    ['factorial', 5]
]), env) == 120
```


### And you're done!

You're off to the races. You've just written a bytecode interpreter in Python
or whatever language you are using to follow along. There are many ways to
extend and improve it. Maybe those will be the subject of a future post. Here
are a few I can think of:

* Make a JIT compiler --- generate native code instead of bytecode
* Re-write this in a language whose runtime is faster than CPython
* Choose a file format and write the compiler and interpreter so that they
  eject to and read from the disk
* Add a foreign-function interface so that the user can call functions from the
  host language
* Expose `compile` and `eval` in the environment to add metaprogramming to your
  language
* Add macros
* Learn from CPython and store local variables in the frame itself instead of
  in a dict --- this is much faster (see `LOAD_FAST` and `STORE_FAST`)
* Target an existing VM's bytecode so you can compile to, say, actual CPython
  or Lua or something
* Similar to above: target [OPy][opy] so you can run compiled programs with
  Andy Chu's VM
* Implement this in a statically-typed language like OCaml, SML, Haskell,
  Rust, Swift, etc

[opy]: https://github.com/oilshell/oil/tree/master/opy

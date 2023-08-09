---
title: "Compiling a Lisp: Reader"
layout: post
date: 2020-09-21 22:00:00 PDT
description: Adding a reader to our Lisp compiler
og_image: /assets/img/compiling-a-lisp-og-image.png
series: compiling-a-lisp
---

<span data-nosnippet>
*[first](/blog/compiling-a-lisp-0/)* -- *[previous](/blog/compiling-a-lisp-5/)*
</span>

Welcome back to the "Compiling a Lisp" series. This time I want to take a break
from compiling and finally add a *reader*. I'm finally getting frustrated
manually entering increasinly complicated ASTs, so I figure it is time. After
this post, we'll be able to type in programs like:

```common-lisp
(< (+ 1 2) (- 4 3))
```

and have our compiler make ASTs for us! Magic. This will also add some nice
debugging tools for us. For example, imagine an interactive command line
utility in which we can enter Lisp expressions and the compiler prints out
human-readable assembly (and hex? maybe?). It could even run the code, too.
Check out this imaginary demo:

```
lisp> 1
; mov rax, 0x4
=> 1
lisp> (add1 1)
; mov rax, 0x4
; add rax, 0x4
=> 2
lisp>
```

Wow, what a thought.

### The Reader interface

To make this interface as simple and testable as possible, I want the reader
interface to take in a C string and return an `ASTNode *`:

```c
ASTNode *Reader_read(char *input);
```

We can add interfaces later to support reading from a `FILE*` or file
descriptor or something, but for now we'll just use strings and line-based
input.

On success, we'll return a fully-formed `ASTNode*`. But on error, well, hold
on. We can't just return `NULL`. On many platforms, `NULL` is defined to be
`0`, which is how we encode the integer `0`. On others, it could be defined to
be `0x55555555`[^1] or something equally silly. Regardless, its value might
overlap with our type encoding scheme in some unintended way.

This means that we have to go ahead and add another immediate object: an
`Error` object. We have some open immediate tag bits, so sure, why not. We can
also use this to signal runtime errors and other fun things. It'll probably be
useful.

### The Error object

Back to the object tag diagram. Below I have reproduced the tag diagram from
previous posts, but now with a new entry (denoted by `<-`). This new entry
shows the encoding for the canonical `Error` object.

```
High                                                         Low
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX00  Integer
0000000000000000000000000000000000000000000000000XXXXXXX00001111  Character
00000000000000000000000000000000000000000000000000000000X0011111  Boolean
0000000000000000000000000000000000000000000000000000000000101111  Nil
0000000000000000000000000000000000000000000000000000000000111111  Error <-
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX001  Pair
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX010  Vector
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX011  String
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX101  Symbol
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX110  Closure
```

If we wanted to, we could even add additional tag bits to the (currently all 0)
payload, to signal different kinds of errors. Maybe later. For now, we add a
tag constant and associated `Object` and `AST` functions:

```c
const unsigned int kErrorTag = 0x3f; // 0b111111
uword Object_error() { return kErrorTag; }

bool AST_is_error(ASTNode *node) { return (uword)node == Object_error(); }
ASTNode *AST_error() { return (ASTNode *)Object_error(); }
```

That should be enough to get us going for now. Perhaps we could even convert
our `Compile_` suite of functions to use this object instead of an `int`. It
would certainly be more informative. Maybe in a future post.

### Language syntax

Let's get back to business and think about what we want our language to look
like. This is a Lisp series but really you could adapt your reader to read any
sort of syntax. No need for parentheses if you're allergic.

I'm going to use this simple Lisp reader because it's short and simple, so
we'll have some parens.

First, our integers will look like integers in most languages --- `0`, `123`,
`-123`.

You can add support for other bases if you like, but I don't plan on it here.

Second, our characters will look like C characters --- `'a'`, `'b'`, etc. Some
implementations opt for `#'a` but that has always looked funky to me.

Third, our booleans will be `#t` and `#f`. You're also welcome to go ahead and
use symbols to represent the names, avoid special syntax, and have those
symbols evaluate to truthy and falsey values.

Fourth, the nil object will be `()`. We can also later bind the symbol `nil` to
mean `()`, too.

I'm going to skip error objects, because they don't yet have any sort of
user-land meaning yet --- they're just used in compiler infrastructure right
now.

Fifth, pairs will look like `(1 2 3)`, meaning `(cons 1 (cons 2 (cons 3
nil)))`. I don't plan on adding support for dotted pair syntax. Whitespace will
be insignificant.

Sixth, symbols will look like any old ASCII identifier: `hello`, `world`,
`fooBar`. I'll also include some punctuation in there, too, so we can use `+`
and `-` as symbols, for example. Or we could even go full Lisp and use
`train-case` identifiers.

I'm going to skip closures, since they don't have a syntactic representation
--- they are just objects known to the runtime. Vectors and strings don't have
any implementation right now so we'll add those to the reader later.

That's it! Key points are: mind your plus and minus signs since they can appear
in both integers and symbols; don't read off the end; have fun.

### The Reader implementation

Now that we've rather informally specified what our language looks like, we can
write a small reader. We'll start with the `Reader_read` function from above.

This function will just be a shell around an internal function with some more
parameters.

```c
ASTNode *Reader_read(char *input) {
  word pos = 0;
  return read_rec(input, &pos);
}
```

This is because we need to carry around some more state to read through this
string. We need to know how far into the string we are. I chose to use an
additional `word` for the index. Some might prefer a `char**` instead. Up to
you.

With any recursive reader invocation, we should advance through all the
whitespace, because it doesn't mean anything to us. For this reason, we have a
handy-dandy `skip_whitespace` function that reads through all the whitespace
and then returns the next non-whitespace character.

```c
void advance(word *pos) { ++*pos; }

char next(char *input, word *pos) {
  advance(pos);
  return input[*pos];
}

char skip_whitespace(char *input, word *pos) {
  char c = '\0';
  for (c = input[*pos]; isspace(c); c = next(input, pos)) {
    ;
  }
  return c;
}
```

We can use `skip_whitespace` in the `read_rec` function to fetch the next
non-whitespace character. Then we'll use that character (and sometimes the
following one, too) to determine what structure we're about to read.

```c
bool starts_symbol(char c) {
  switch (c) {
  case '+':
  case '-':
  case '*':
  case '>':
  case '=':
  case '?':
    return true;
  default:
    return isalpha(c);
  }
}

ASTNode *read_rec(char *input, word *pos) {
  char c = skip_whitespace(input, pos);
  if (isdigit(c)) {
    return read_integer(input, pos, /*sign=*/1);
  }
  if (c == '+' && isdigit(input[*pos + 1])) {
    advance(pos); // skip '+'
    return read_integer(input, pos, /*sign=*/1);
  }
  if (c == '-' && isdigit(input[*pos + 1])) {
    advance(pos); // skip '-'
    return read_integer(input, pos, /*sign=*/-1);
  }
  if (starts_symbol(c)) {
    return read_symbol(input, pos);
  }
  if (c == '\'') {
    advance(pos); // skip '\''
    return read_char(input, pos);
  }
  if (c == '#' && input[*pos + 1] == 't') {
    advance(pos); // skip '#'
    advance(pos); // skip 't'
    return AST_new_bool(true);
  }
  if (c == '#' && input[*pos + 1] == 'f') {
    advance(pos); // skip '#'
    advance(pos); // skip 'f'
    return AST_new_bool(false);
  }
  if (c == '(') {
    advance(pos); // skip '('
    return read_list(input, pos);
  }
  return AST_error();
}
```

Note that I put the integer cases above the symbol case because we want to
catch `-123` as an integer instead of a symbol, and `-a123` as a symbol instead
of an integer.

We'll probably add more entries to `starts_symbol` later, but those should
cover the names we've used so far.

For each type of subcase (integer, symbol, list), the basic idea is the same:
while we're still inside the subcase, add on to it.

For integers, this means multiplying and adding (concatenating digits, so to
speak):

```c
ASTNode *read_integer(char *input, word *pos, int sign) {
  char c = '\0';
  word result = 0;
  for (char c = input[*pos]; isdigit(c); c = next(input, pos)) {
    result *= 10;
    result += c - '0';
  }
  return AST_new_integer(sign * result);
}
```

It also takes a sign parameter so if we see an explicit `-`, we can negate the
integer.

For symbols, this means reading characters into a C string buffer:

```c
const word ATOM_MAX = 32;

bool is_symbol_char(char c) {
  return starts_symbol(c) || isdigit(c);
}

ASTNode *read_symbol(char *input, word *pos) {
  char buf[ATOM_MAX + 1]; // +1 for NUL
  word length = 0;
  for (length = 0; length < ATOM_MAX && is_symbol_char(input[*pos]); length++) {
    buf[length] = input[*pos];
    advance(pos);
  }
  buf[length] = '\0';
  return AST_new_symbol(buf);
}
```

For simplicity's sake, I avoided dynamic resizing. We only get at most symbols
of size 32. Oh well.

Note that symbols can also have trailing numbers in them, just not at the front
--- like `add1`.

For characters, we only have three potential input characters to look at:
quote, char, quote. No need for a loop:

```c
ASTNode *read_char(char *input, word *pos) {
  char c = input[*pos];
  if (c == '\'') {
    return AST_error();
  }
  advance(pos);
  if (input[*pos] != '\'') {
    return AST_error();
  }
  advance(pos);
  return AST_new_char(c);
}
```

This means that input like `''` or `'aa'` will be an error.

For booleans, we can tackle those inline because there's only two cases and
they're both trivial. Check for `#t` and `#f`. Done.

And last, for lists, it means we recursively build up pairs until we get to
`nil`:

```c
ASTNode *read_list(char *input, word *pos) {
  char c = skip_whitespace(input, pos);
  if (c == ')') {
    advance(pos);
    return AST_nil();
  }
  ASTNode *car = read_rec(input, pos);
  assert(car != AST_error());
  ASTNode *cdr = read_list(input, pos);
  assert(cdr != AST_error());
  return AST_new_pair(car, cdr);
}
```

Note that we still have to skip whitespace in the beginning so that we catch
cases that have space either right after an opening parenthesis or right before
a closing parenthesis. Or both!

That's it --- that's the whole parser. Now let's write some tests.

### Tests

I added a new suite for reader tests. I figure it's nice to have them grouped.
Here are some of the trickier tests from that suite that originally tripped me
up one way or another.

Negative integers originally parsed as symbols until I figured out I had to
flip the case order:

```c
TEST read_with_negative_integer_returns_integer(void) {
  char *input = "-1234";
  ASTNode *node = Reader_read(input);
  ASSERT_IS_INT_EQ(node, -1234);
  AST_heap_free(node);
  PASS();
}
```

Oh, and the `ASSERT_IS_INT_EQ` and upcoming `ASSERT_IS_SYM_EQ` macros are
helpers that assert the type and value are as expected.

I also forgot about leading whitespace for a while:

```c
TEST read_with_leading_whitespace_ignores_whitespace(void) {
  char *input = "   \t   \n  1234";
  ASTNode *node = Reader_read(input);
  ASSERT_IS_INT_EQ(node, 1234);
  AST_heap_free(node);
  PASS();
}
```

And also whitespace in lists:

```c
TEST read_with_list_returns_list(void) {
  char *input = "( 1 2 0 )";
  ASTNode *node = Reader_read(input);
  ASSERT(AST_is_pair(node));
  ASSERT_IS_INT_EQ(AST_pair_car(node), 1);
  ASSERT_IS_INT_EQ(AST_pair_car(AST_pair_cdr(node)), 2);
  ASSERT_IS_INT_EQ(AST_pair_car(AST_pair_cdr(AST_pair_cdr(node))), 0);
  ASSERT(AST_is_nil(AST_pair_cdr(AST_pair_cdr(AST_pair_cdr(node)))));
  AST_heap_free(node);
  PASS();
}
```

And here's some goofy symbol to make sure all these symbol characters work:

```c
TEST read_with_symbol_returns_symbol(void) {
  char *input = "hello?+-*=>";
  ASTNode *node = Reader_read(input);
  ASSERT_IS_SYM_EQ(node, "hello?+-*=>");
  AST_heap_free(node);
  PASS();
}
```

And to make sure trailing digits in symbol names work:

```c
TEST read_with_symbol_with_trailing_digits(void) {
  char *input = "add1 1";
  ASTNode *node = Reader_read(input);
  ASSERT_IS_SYM_EQ(node, "add1");
  AST_heap_free(node);
  PASS();
}
```

Nice.

### Some extras

Now, we could wrap up with the tests, but I did mention some fun features like
a REPL. Here's a function `repl` that you can call from your `main` function
instead of running the tests.

```c
int repl() {
  do {
    // Read a line
    fprintf(stdout, "lisp> ");
    char *line = NULL;
    size_t size = 0;
    ssize_t nchars = getline(&line, &size, stdin);
    if (nchars < 0) {
      fprintf(stderr, "Goodbye.\n");
      free(line);
      break;
    }

    // Parse the line
    ASTNode *node = Reader_read(line);
    free(line);
    if (AST_is_error(node)) {
      fprintf(stderr, "Parse error.\n");
      continue;
    }

    // Compile the line
    Buffer buf;
    Buffer_init(&buf, 1);
    int result = Compile_expr(&buf, node, /*stack_index=*/-kWordSize);
    AST_heap_free(node);
    if (result < 0) {
      fprintf(stderr, "Compile error.\n");
      Buffer_deinit(&buf);
      continue;
    }

    // Print the assembled code
    for (size_t i = 0; i < buf.len; i++) {
      fprintf(stderr, "%.02x ", buf.address[i]);
    }
    fprintf(stderr, "\n");

    Buffer_deinit(&buf);
  } while (true);
  return 0;
}
```

And we can trigger this mode by passing `--repl-assembly`:

```c
int run_tests(int argc, char **argv) {
  GREATEST_MAIN_BEGIN();
  RUN_SUITE(object_tests);
  RUN_SUITE(ast_tests);
  RUN_SUITE(reader_tests);
  RUN_SUITE(buffer_tests);
  RUN_SUITE(compiler_tests);
  GREATEST_MAIN_END();
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--repl-assembly") == 0) {
    return repl();
  }
  return run_tests(argc, argv);
}
```

It uses all the machinery from the last couple posts and then prints out the
results in hex pairs. Interactions look like this:

```
sequoia% ./bin/compiling-reader --repl-assembly
lisp> 1
48 c7 c0 04 00 00 00 
lisp> (add1 1)
48 c7 c0 04 00 00 00 48 05 04 00 00 00 
lisp> 'a'
48 c7 c0 0f 61 00 00
lisp> Goodbye.
sequoia% 
```

Excellent. A fun exercise for the reader might be going further and executing
the compiled code and printing the result, as above. The trickiest (because we
don't have infrastructure for that yet) part of it will be printing the result,
I think.

Another fun exercise might be adding a mode to the compiler to print text
assembly to the screen, like a debugging trace. This should be straightforward
enough since we already have very specific opcode implementations.

Anyway, thanks for reading. Next time we'll get back to compiling and tackle
[let-expressions](/blog/compiling-a-lisp-7/).

{% include compiling_a_lisp_toc.md %}

[^1]: See [this series of
      Tweets](https://twitter.com/thingskatedid/status/1293476425454895105) by
      Kate about changing the value of `NULL` in the TenDRA compiler.

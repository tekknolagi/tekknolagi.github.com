---
title: Parsing to IR and lvalues
layout: post
---

I don't want an AST in my hobby compiler. I'm not going to use it for
analysis---it would just be a stepping stone representation on the way to the
thing I really want, which is an SSA CFG. So I decided to parse straight from
tokens into an SSA IR. I ran into some issues, fixed them, and would like to
share the implementation. Maybe it's a normal strategy, maybe it's not.

## Precedence climbing

I've always been a little nervous around parsers so I decided to lean into it
and get familiar with precedence climbing. I'd already written [something
similar][diff-py] for a little math AST (**update:** see also [this
one](/blog/precedence-climbing/)) so I figured I would be able to reasonably
easily port that to my new project.

[diff-py]: https://gist.github.com/tekknolagi/b587de40ea55dc9d65b70282fb58e262#file-diff-py-L531

I was mostly right. For example, here is the (fairly direct) Rust port that
turns expressions into IR instead of AST. The only difference is that instead
of pointers, nodes hold onto IDs, and they are linearized in a big
`Vec`[^munificent-bytecode] (hence `push_op` and `push_insn`).

[^munificent-bytecode]: For more reading on this, check out [Flattening
    ASTs][flattening-asts] by Adrian Sampson and [this Reddit
    comment][munificent-reddit] by Bob Nystrom of Crafting Interpreters fame.
    The comment is reproduced here:

    (quoting the OP)

    > I can imagine, while parsing, whenever you created a new node, rather
    > than allocating space just for it and giving the parent node the pointer,
    > you could push the node onto a vector or some other such growable
    > contiguous array and give the parent a reference to that spot.

    Yes, that will definitely help! Once you do that, you may consider other stepwise refinements.

    * Since subexpressions are executed first, it makes sense to have them
      earlier in the array than their parents. So instead of walking from the
      root to the leaves, walk the leaves first and then have the parents
      follow them.
    * At that point, parents no longer need references to their children.
      Instead, you just need some convenient place to store the results of the
      child evaluations. Maybe a stack.
    * Now your array of instructions doesn't need any actual links between
      them. It's just a flat list of which operations to perform.

    Ta-da, you just reinvented stack-based bytecode.


[flattening-asts]: https://www.cs.cornell.edu/~asampson/blog/flattening.html
[munificent-reddit]: https://old.reddit.com/r/ProgrammingLanguages/comments/mrifdr/treewalking_interpreters_and_cachelocality/gumsi2v/

```rust
impl Parser<'_> {
    // Recursive parse function for expressions (statements handled elsewhere)
    fn parse_(&mut self, mut env: &mut Env, prec: u32)
        -> Result<InsnId, ParseError> {

        let mut lhs = match self.tokens.next() {
            None => return Err(ParseError::UnexpectedEof),
            Some(Token::Bool(value)) => {
                self.push_op(Opcode::Const(Value::Bool(value)))
            }
            // ...
            Some(Token::LParen) => {
                let result = self.parse_(&mut env, 0)?;
                self.expect(Token::RParen)?;
                result
            }
            Some(token) => return Err(ParseError::UnexpectedToken(token)),
        };
        while let Some(token) = self.tokens.peek() {
            let (assoc, op_prec) = match token {
                // ...
                Token::Plus => (Assoc::Any, 4),
                Token::Minus => (Assoc::Left, 4),
                Token::Star => (Assoc::Any, 5),
                // ...
                _ => break,
            };
            let token = token.clone();
            if op_prec < prec { return lhs; }
            self.tokens.next();
            let next_prec =
                if assoc == Assoc::Left { op_prec + 1 } else { op_prec };
            // ...
            let opcode = match token {
                // ...
                Token::Plus => Opcode::Add,
                Token::Minus => Opcode::Sub,
                Token::Star => Opcode::Mul,
                _ => panic!("Unexpected token {token:?}"),
            };
            let rhs = self.parse_(&mut env, next_prec)?;
            lhs = self.push_insn(opcode, smallvec![lhs, rhs]);
        }
        lhs
    }
}
```

This started mostly fine (parsing `1 + 2` to an AST is very similar to parsing
to the IR) but I quickly ran into a problem: what about names?

See, evaluating a name like `abc` in the expression `1 + abc` is fine if you
have all of the environment plumbing: look it up in the `env`. In this
compiler, that means find the stack slot that we've assigned for it in this
scope and emit a load from that stack slot.

```rust
impl Parser<'_> {
    fn parse_(&mut self, mut env: &mut Env, prec: u32)
        -> Result<InsnId, ParseError> {

        let mut lhs = match self.tokens.next() {
            None => return Err(ParseError::UnexpectedEof),
            // ...
            Some(Token::Ident(name)) => {
                let name = self.prog.intern(&name);
                let slot = env.lookup(name)?;
                self.push_insn(Opcode::Load(slot), smallvec![self.frame()])
            }
            // ...
            Some(token) => return Err(ParseError::UnexpectedToken(token)),
        };
        // ...
    }
}
```

But assignments to names are also expressions! `a = 5` is a perfectly valid
expression and we should not evaluate `a`. In fact, we should find the stack
slot of `a`, wait until it's assigned to, and *then* generate a store (or if
it's not assigned to, emit the load).

## Names and assignment

Here's the subtle thing (at least for me): parsing to an AST doesn't have this
problem because it implies two things:

1. Recursively building up expressions without committing them immediately to a
   buffer of IR
1. Leaving names unresolved, as strings (imagine a `Name "foo"` AST node or
   something)

In IR land, we're trying to eagerly emit IR *and* resolve names to offsets at
the same time.

The way I solved this first was by doing some kind of lookahead in the `Ident`
case for `lhs`. If the next token is an equals sign, parse right and emit an
assignment. That worked fine for a bit, but has two problems:

1. It doesn't handle precedence well---ideally we would have all the
   precedence logic be in the existing precedence climbing loop instead
1. It breaks down for more complicated assignments like assignments to
   attributes

For attribute assignments like `a.b.c = 5`, we *do* want to evaluate `a.b`, but
we *do not* want to evaluate `a.b.c`. We want to wait until we either see an
equals sign (and *store* to `a.b.c`) or see this expression get used elsewhere
and then do the load then.

After talking with [Andy Chu][andy] of [Oils][oils] fame (whose name is the
first that comes to mind when I think of precedence climbing) and some faffing
about, I think I figured out a neat enough API for this.

[andy]: https://andychu.net/
[oils]: https://oils.pub/

## LValues

The key is to be able to defer evaluation just a little longer until we know
more about the context. To do that, we add an enum that can hold one of three
things (or more, if you allow array assignment):

```rust
pub enum LValue {
    Insn(InsnId),
    Name(NameId),
    Attr(InsnId, NameId),
}
```

The `Insn` case represents an evaluated expression. It can be used for its
value immediately.

The `Name` case represents an *un-evaluated name* for use in either loads or
stores.

The `Attr` case represents an *un-evaluated attribute lookup* for use in either
loads or stores. The left hand side (the receiver, if you will) has already
been evaluated---it's an `InsnId`, and therefore also committed to the IR
buffer---but the right hand side has not.

This `LValue` enum will only be used temporarily in the `lhs` variable. We add
two new special cases to handle it:

1. If the token from the precedence climbing is an equals sign, generate
   assignment code (`Store` or `StoreAttr` in this case)
2. If the token from the precedence climbing is a dot, generate an `Attr`
   LValue

If we ever want to return this `LValue` from `parse_` or use it as part of a
larger expression (say, `lhs + rhs`), we will need to convert it to an `InsnId`
by evaluating it. That function is called `lvalue_as_rvalue`.

```rust
impl Parser<'_> {
    fn parse_(&mut self, mut env: &mut Env, prec: u32)
        -> Result<InsnId, ParseError> {

        let mut lhs = match self.tokens.next() {
            None => return Err(ParseError::UnexpectedEof),
            // ...
            Some(Token::Bool(value)) => {
                LValue::Insn(self.push_op(Opcode::Const(Value::Bool(value))))
            }
            Some(Token::Ident(name)) => {
                LValue::Name(self.prog.intern(&name))
            }
            // ...
            Some(token) => return Err(ParseError::UnexpectedToken(token)),
        };
        while let Some(token) = self.tokens.peek() {
            let (assoc, op_prec) = match token {
                Token::Equal => (Assoc::Right, 0),  // NEW!
                // ...
                Token::Plus => (Assoc::Any, 4),
                // ...
                _ => break,
            };
            let token = token.clone();
            if op_prec < prec { return self.lvalue_as_rvalue(env, lhs); }
            self.tokens.next();
            let next_prec =
                if assoc == Assoc::Left { op_prec + 1 } else { op_prec };
            // Special case: we need to look up the frame slot and write to it
            // or assign to the attribute
            if token == Token::Equal {  // NEW!
                lhs = match lhs {
                    LValue::Insn(..) =>
                        return Err(ParseError::CannotAssignTo(lhs)),
                    LValue::Name(name) => {
                        let rhs = self.parse_(&mut env, next_prec)?;
                        self.store_local(&mut env, name, rhs)?;
                        lhs
                    }
                    LValue::Attr(obj, name) => {
                        let rhs = self.parse_(&mut env, next_prec)?;
                        let obj = self.push_insn(Opcode::GuardInstance, smallvec![obj]);
                        self.push_insn(Opcode::StoreAttr(name), smallvec![obj, rhs]);
                        LValue::Insn(rhs)
                    }
                };
                continue;
            }
            // Special case: we need to hold off evaluating the attribute until
            // it is read elsewhere or written
            if token == Token::Dot {  // NEW!
                let name = self.expect_ident()?;
                let obj = self.lvalue_as_rvalue(env, lhs)?;
                lhs = LValue::Attr(obj, name);
                continue;
            }
            // Evaluate the lvalue because we want to use it in a binary
            // operation
            let mut lhs_value = self.lvalue_as_rvalue(env, lhs)?;  // NEW!
            // ...
            lhs = LValue::Insn(self.push_insn(opcode, smallvec![lhs_value, rhs]));
        }
        self.lvalue_as_rvalue(env, lhs)  // NEW!
    }

    fn lvalue_as_rvalue(&mut self, env: &Env, lvalue: LValue)
        -> Result<InsnId, ParseError> {

        match lvalue {
            LValue::Insn(insn_id) => Ok(insn_id),
            LValue::Name(name) => self.load_local(env, name),
            LValue::Attr(obj, name) => {
                let obj = self.push_insn(Opcode::GuardInstance, smallvec![obj]);
                Ok(self.push_insn(Opcode::LoadAttr(name), smallvec![obj]))
            }
        }
    }
}
```

This seems to work pretty well for all of my parser tests and also seems
extensible to array assignments (if I add them in the future).

In conclusion, I feel a little less nervous around parsers now.

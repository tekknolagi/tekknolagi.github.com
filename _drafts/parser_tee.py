# # A precedence climbing parser in Python
#
# **License:** MIT
# **Copyright:** (c) 2025 [Maxwell Bernstein](https://bernsteinbear.com)

# A list of operators in groups of increasing precedence (tighter binding
# power). It doesn't matter exactly what the precedence is, just that each
# entry within a group has the same precedence (+ and -, etc) and that each
# group is lower precedence than the next.
OPERATORS = [
    [("+", "any"), ("-", "left")],
    [("*", "any"), ("/", "left")],
    [("^", "right")],
    [("(", "left")],
]

# Pull out the operator names from each entry.
OPERATOR_NAMES = [info[0] for group in OPERATORS for info in group]


class ParseError(Exception):
    pass


import itertools


def peek(tee_iterator):
    """Return the next value without moving the input forward. From CPython
    docs: https://docs.python.org/3/library/itertools.html#itertools.tee"""
    [forked_iterator] = itertools.tee(tee_iterator, 1)
    return next(forked_iterator)


def at_end(tee_iterator):
    try:
        peek(tee_iterator)
        return False
    except StopIteration:
        return True


# Turn an input string into a list of tokens.
def tokenize(source: str) -> list:
    if not source.isascii():
        raise ParseError("Only ASCII characters are supported")
    result: list = []
    [source_iter] = itertools.tee(source, 1)

    while not at_end(source_iter):
        c = next(source_iter)
        # Skip all whitespace between tokens
        if c.isspace():
            continue
        # Read a number and append it as an int
        if c.isdigit():
            num = int(c)
            while not at_end(source_iter) and peek(source_iter).isdigit():
                num = num * 10 + int(next(source_iter))
            result.append(num)
            continue
        # Read an operator and append it as a string
        if c in OPERATOR_NAMES or c in "(),":
            result.append(c)
            continue
        # Read a variable name and append it as a string
        if c.isalpha():
            var = c
            while not at_end(source_iter) and peek(source_iter).isalpha():
                var += next(source_iter)
            result.append(var)
            continue
        raise ParseError(f"Unexpected character: {c}")
    return result


# Pull out the precedence for each entry by using its group's index in
# `OPERATORS`. This gives operators `*` and `/` precendece 1, for example.
OPERATOR_PREC = {
    info[0]: idx for (idx, group) in enumerate(OPERATORS) for info in group
}

# Pull out the associativity for each entry.
OPERATOR_ASSOC = {info[0]: info[1] for group in OPERATORS for info in group}


# Precedence climbing is composed of a recursive function parse_ that also
# contains a loop. The loop is responsible for reading all tokens at or above
# the given precedence level. When it sees a token below the minimum required
# precedence, it drops down a precedence level by returning. Otherwise, it uses
# the current operator's precedence as the minimum precedence for the next
# tokens (by recursively calling).
#
# This parser treats the `tokens` list as a mutable stream of tokens by doing
# `pop(0)` repeatedly. In a real parser, use a deque or stream and don't
# actually pop from the front of a `list`.
def parse_(tokens: list, min_prec: int):
    # Atomic groups such as integers and parenthesized sub-expressions make up
    # the left hand side of an expression.
    def atom():
        if at_end(tokens):
            raise ParseError("Unexpected end of input")
        token = next(tokens)
        if token == "-":
            # We only support unary negation, not unary addition.
            return ["negate", atom()]
        if token == "(":
            # Parse a parenthesized expression.
            result = parse_(tokens, 0)
            if at_end(tokens) or next(tokens) != ")":
                raise ParseError("Expected closing parenthesis")
            return result
        if token in OPERATOR_NAMES:
            raise ParseError(f"Unexpected operator: {token}")
        if isinstance(token, int):
            return token
        if isinstance(token, str) and token.isalpha():
            # Return a variable name.
            return token
        raise ParseError(f"Unexpected token: {token}")

    def comma_separated():
        if at_end(tokens):
            raise ParseError("Expected closing parenthesis in function call")
        if peek(tokens) == ")":
            # Empty argument list
            return []
        # At least one argument
        result = [parse_(tokens, 0)]
        # If there are more, each argument starts with a comma
        while not at_end(tokens) and peek(tokens) == ",":
            next(tokens)
            result.append(parse_(tokens, 0))
        return result

    lhs = atom()
    # The main precedence climbing loop.
    while not at_end(tokens) and (token := peek(tokens)) in OPERATOR_NAMES:
        op_prec = OPERATOR_PREC[token]
        if op_prec < min_prec:
            # Drop a precedence level by returning.
            return lhs
        next(tokens)
        # Special-case function application: the function arguments can re-start
        # the precedence climbing at precedence `0`.
        if token == "(":
            args = comma_separated()
            if at_end(tokens) or next(tokens) != ")":
                raise ParseError("Expected closing parenthesis in function call")
            lhs = [lhs, *args]
            continue
        # For left-associative operators such as `-` and `/`, bump up the
        # minimum precedence by one. Don't bump for any-associative operators
        # such as `+` or right-associative operators such as `^`.
        next_prec = op_prec + 1 if OPERATOR_ASSOC[token] == "left" else op_prec
        rhs = parse_(tokens, next_prec)
        # This is where you could build an AST node instead of making a list.
        lhs = [token, lhs, rhs]
    return lhs


def parse(tokens: list):
    # Start off with minimum precedence of `0`.
    [tokens_iter] = itertools.tee(tokens, 1)
    result = parse_(tokens_iter, 0)
    if not at_end(tokens_iter):
        # If we have tokens left-over, that's a syntax error.
        raise ParseError("Unexpected tokens: " + " ".join(map(str, tokens_iter)))
    return result


# Also include a bunch of tests that push the entirety of the example over 200
# lines but are illustrative.
import re
import unittest


class TokenizerTests(unittest.TestCase):
    def test_empty(self):
        self.assertEqual(tokenize(""), [])

    def test_digit(self):
        self.assertEqual(tokenize("1"), [1])

    def test_number(self):
        self.assertEqual(tokenize("123"), [123])

    def test_add(self):
        self.assertEqual(tokenize("+"), ["+"])

    def test_sub(self):
        self.assertEqual(tokenize("-"), ["-"])

    def test_sub(self):
        self.assertEqual(tokenize("-"), ["-"])

    def test_mul(self):
        self.assertEqual(tokenize("*"), ["*"])

    def test_div(self):
        self.assertEqual(tokenize("/"), ["/"])

    def test_pow(self):
        self.assertEqual(tokenize("^"), ["^"])

    def test_comma(self):
        self.assertEqual(tokenize(","), [","])

    def test_unrecognized_operator(self):
        with self.assertRaises(ParseError):
            tokenize("%")

    def test_var(self):
        self.assertEqual(tokenize("a"), ["a"])
        self.assertEqual(tokenize("abc"), ["abc"])


class ParseTests(unittest.TestCase):
    def assert_parse_error(self, message):
        return self.assertRaisesRegex(ParseError, re.escape(message))

    def test_empty(self):
        with self.assert_parse_error("Unexpected end of input"):
            parse([])

    def test_const(self) -> None:
        self.assertEqual(parse([3]), 3)

    def test_var(self) -> None:
        self.assertEqual(parse(["abc"]), "abc")

    def test_const_leftover_raises(self) -> None:
        with self.assert_parse_error("Unexpected tokens: 4 5"):
            parse([3, 4, 5])

    def test_const_paren(self) -> None:
        self.assertEqual(parse(["(", 3, ")"]), 3)

    def test_const_paren_missing(self) -> None:
        with self.assert_parse_error("Expected closing parenthesis"):
            parse(["(", 3])

        with self.assert_parse_error("Unexpected tokens: )"):
            parse([3, ")"])

    def test_call_fun(self) -> None:
        self.assertEqual(parse(["f", "(", "x", ")"]), ["f", "x"])

    def test_call_fun_no_arguments(self) -> None:
        self.assertEqual(parse(["f", "(", ")"]), ["f"])

    def test_call_fun_missing_closing_paren(self) -> None:
        with self.assert_parse_error("Expected closing parenthesis in function call"):
            parse(["f", "("])

        with self.assert_parse_error("Expected closing parenthesis in function call"):
            parse(["f", "(", "x"])

    def test_call_fun_need_comma(self) -> None:
        with self.assert_parse_error("Expected closing parenthesis in function call"):
            parse(["f", "(", "x", "y", ")"])

    def test_call_fun_double_comma(self) -> None:
        with self.assert_parse_error("Unexpected token: ,"):
            parse(["f", "(", "x", ",", ",", "y", ")"])

    def test_call_fun_more_than_one_argument(self) -> None:
        self.assertEqual(parse(["f", "(", "x", ",", "y", ")"]), ["f", "x", "y"])

    def test_negate_const(self) -> None:
        with self.assert_parse_error("Unexpected end of input"):
            parse(["-"])
        self.assertEqual(parse(["-", 3]), ["negate", 3])

    def test_add(self) -> None:
        self.assertEqual(parse([1, "+", 2]), ["+", 1, 2])

    def test_add_negate(self) -> None:
        self.assertEqual(parse([1, "+", "-", 2]), ["+", 1, ["negate", 2]])

    def test_mul_negate(self) -> None:
        self.assertEqual(parse([1, "*", "-", 2]), ["*", 1, ["negate", 2]])

    def test_sub_negate(self) -> None:
        self.assertEqual(parse([1, "-", "-", 2]), ["-", 1, ["negate", 2]])

    def test_begin_add_raises(self) -> None:
        with self.assert_parse_error("Unexpected operator: +"):
            parse(["+"])

        with self.assert_parse_error("Unexpected operator: +"):
            parse(["+", 2])

    def test_double_add_raises(self) -> None:
        with self.assert_parse_error("Unexpected operator: +"):
            parse([1, "+", "+", 2])

    def test_add_add(self) -> None:
        self.assertEqual(parse([1, "+", 2, "+", 3]), ["+", 1, ["+", 2, 3]])

    def test_add_mul(self) -> None:
        self.assertEqual(parse([1, "+", 2, "*", 3]), ["+", 1, ["*", 2, 3]])
        self.assertEqual(parse(["(", 1, "+", 2, ")", "*", 3]), ["*", ["+", 1, 2], 3])

    def test_mul_add(self) -> None:
        self.assertEqual(parse([1, "*", 2, "+", 3]), ["+", ["*", 1, 2], 3])

    def test_sub(self) -> None:
        self.assertEqual(parse([1, "-", 2]), ["-", 1, 2])

    def test_sub_sub(self) -> None:
        self.assertEqual(parse([1, "-", 2, "-", 3]), ["-", ["-", 1, 2], 3])

    def test_add_mul(self) -> None:
        self.assertEqual(parse([1, "+", 2, "*", 3]), ["+", 1, ["*", 2, 3]])
        self.assertEqual(parse([1, "*", 2, "+", 3]), ["+", ["*", 1, 2], 3])


class EndToEndTests(unittest.TestCase):
    def parse(self, source: str) -> list:
        return parse(tokenize(source))

    def test_int(self):
        self.assertEqual(self.parse("123"), 123)

    def test_add(self):
        self.assertEqual(self.parse("3+4"), ["+", 3, 4])

    def test_add_call(self):
        self.assertEqual(self.parse("1+f(2)*3"), ["+", 1, ["*", ["f", 2], 3]])
        self.assertEqual(self.parse("1*f(2)+3"), ["+", ["*", 1, ["f", 2]], 3])

    def test_call_call(self):
        self.assertEqual(self.parse("f(1)(2)"), [["f", 1], 2])

    def test_call0(self):
        self.assertEqual(self.parse("f()"), ["f"])

    def test_call1(self):
        self.assertEqual(self.parse("f(x)"), ["f", "x"])

    def test_call2(self):
        self.assertEqual(self.parse("f(x, y)"), ["f", "x", "y"])

    def test_call3(self):
        self.assertEqual(self.parse("f(x, y, z)"), ["f", "x", "y", "z"])

    def test_call_expression_argument(self):
        self.assertEqual(self.parse("f(1+2, 3*4)"), ["f", ["+", 1, 2], ["*", 3, 4]])

    def test_call_nested(self):
        self.assertEqual(self.parse("f(g(x), h(y))"), ["f", ["g", "x"], ["h", "y"]])


if __name__ == "__main__":
    unittest.main()

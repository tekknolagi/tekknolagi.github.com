---
title: "How to shoot yourself in the foot without really trying"
layout: post
date: 2017-10-06 11:42:09 EDT
co_author: Aubrey Anderson
---

I am a teaching assistant for the Data Structures course at my school. I wrote
the reference implementation and a data structure that the students could use
for their first project: `Datum`. `Datum` is supposed to be able to hold
integers, booleans and "codeblocks" (glorified strings) for the small postfix
calculator students are building. The idea is that they have a minimal runtime
type system.

Since this class is taught in C++ and currently the students are trying to
grasp interface / implementation boundaries, I wrote the interface like this:

```c++
class Datum {
    public:
        Datum(const Datum &d);  /* Copy constructor */
        Datum(int i);           /* Make a Datum from an int, bool, or string */
        Datum(bool b);
        Datum(const char *s);
        Datum(std::string s);

        /* These throw exceptions if they are called on the wrong type. */
        int getInt();
        bool getBool();
        std::string getCodeBlock();
        std::string toString();

    private:
    /* Some private variables. */
};
```

so that students could build `Datum`s with expressions like `Datum(true)`,
`Datum(5)`, `Datum("Hello, world!")`.

The first students started posting on the online course forum with problems
like "I am trying to print the top `Datum` on the stack but no matter its
value, the program always prints `true`". Then people came into office hours
because they could not fix their bugs, and I noticed that at least four people
had the same problem. I grabbed Aubrey, another TA, and we started
investigating what was going wrong.

After around 20 minutes, we managed to narrow the problem down to a very
minimal test case:

```c++
#include <iostream>
#include "Datum.h";

int main() {
    Datum d = new Datum(5);
    std::cout << d.toString() << endl;
}
```

People who read too much code for a living will have immediately stopped and
thought, "But Max, that won't even compile. The right side of that expression
returns a pointer, and the left side is not even close to a pointer."

That's true! But as we discovered, the [C++11 standard (PDF)][1] has implicit
type conversions:

> <h4>4.12 Boolean conversions</h4>
> A prvalue of arithmetic, unscoped enumeration, pointer, or pointer to member
> type can be converted to a prvalue of type bool. A zero value, null pointer
> value, or null member pointer value is converted to false; any other value is
> converted to true. For direct-initialization (8.5), a prvalue of type
> std::nullptr_t can be converted to a prvalue of type bool; the resulting
> value is false.

and in our case this happens in the [copy-initialization][2] of `d`.

So what does that mean? It means that the right side (of type `Datum *`), did
not match against `Datum(int)`, `Datum(std::string)`, or `Datum(const char *)`,
but instead matched against `Datum(bool)`. Since it found something to match
against, it did not generate so much as a warning (yes, even with `-Wall
-Wextra -Weverything -pedantic`) and silently confused the heck out of the poor
Data Structures students. The students, who would normally have seen a compiler
error, adjusted the left side to match, and gone on their merry ways were now
left with broken code.

#### The solution

The solution is one word long: add the keyword `explicit` before the bool
constructor:

```c++
class Datum {
    public:
        Datum(const Datum &d);  /* Copy constructor */
        Datum(int i);           /* Make a Datum from an int, bool, or string */
        explicit Datum(bool b);
        Datum(const char *s);
        Datum(std::string s);

    /* Some other stuff. */
};
```

and call it a day. `explicit` ensures that the type must match the constructor
parameter exactly. This way, a compile-error is raised when students attempt
the above code. Another solution, I suppose, is to stop using C++ entirely.

[1]: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3690.pdf
[2]: http://en.cppreference.com/w/cpp/language/copy_initialization

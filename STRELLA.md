# The Strella Language: A Complete Guide

Strella is a small, statically typed, compiled language for the Strella 8-bit
computer. This guide describes its values, syntax, statements, functions,
built-in operations, and compile-time features. The guide is organized as a
sequence of lessons; each section of a guide file is one lesson, and the
lessons build on one another. There are no exercises and no self-check
sections.

## Conventions

- A byte is 8 bits; a word is 16 bits.
- Numeric examples use decimal unless a prefix makes another base clear:
  `0x` for hexadecimal, `0b` for binary, and `0o` for octal.
- Code blocks marked `text` contain Strella source.
- The compiler is called `strella`. Source files conventionally use the
  `.strella` extension.

## Language guide

1. [Overview](docs/guide/01-overview.md) — lessons 1–3: what Strella is, the
   shape of a program, and lexical rules.
2. [Values and expressions](docs/guide/02-values.md) — lessons 4–6: types,
   variables, constants, scope, expressions, and operators.
3. [Control flow](docs/guide/03-control-flow.md) — lesson 7.
4. [Functions](docs/guide/04-functions.md) — lessons 8–10: functions, inline
   functions, stack functions, and recursion.
5. [Built-ins](docs/guide/05-builtins.md) — lessons 11–12: built-in functions
   and device constants.
6. [Preimages and optimization](docs/guide/06-preimages-and-optimization.md) —
   lessons 13–14: startup images, constant folding, and the optimizer.
7. [Pointers and strings](docs/guide/07-pointers-and-strings.md) — lessons
   15–16: pointers, arrays, strings, and run-time behavior.
8. [Grammar and diagnostics](docs/guide/08-grammar-and-diagnostics.md) —
   lessons 17–19: the complete grammar, compiler diagnostics, and a full
   example program.

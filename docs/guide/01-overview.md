# Overview

## Lesson 1 — What Strella is

Strella is a small, statically typed, compiled language whose programs consist
of global constants and variables, functions, and at most one `preimage`
declaration. One function must be `main`, the entry point.

It has:

- integer, boolean, pointer, array, and packed structure types;
- variables and read-only constants;
- arithmetic, bitwise, comparison, and logical operators;
- `if`, `while`, `do`-`while`, `for`, and `switch` statements;
- functions with parameters and return values;
- inline functions and stack-based functions;
- built-in functions for output, input, and display control;
- built-in constants for device configuration;
- compile-time constant evaluation;
- a `preimage` declaration for a startup image.

Strella has no classes, dynamic allocation, exceptions, or modules. It has no
multiplication, division, or remainder operators. A program is compiled ahead of
time; there is no interpreter for the language.

## Lesson 2 — The shape of a program

A source file is a sequence of top-level declarations of three kinds:

- a `preimage` declaration;
- a global variable or constant declaration;
- a function declaration.

A complete program looks like this:

```text
preimage: "splash.png";

const LIMIT: byte = 10;
var counter: byte = 0;

@inline func twice(byte value) byte {
  return value + value;
}

func main() {
  set_devices(display | terminal);
  var local: byte = twice(3);
  put('0' + local);
}
```

Top-level declaration order does not affect name resolution: functions may
call later functions, and globals may be used by earlier functions. A
`preimage` may appear anywhere at top level, but a project allows at most one.

Exactly one function must be named `main`, with no parameters. Its return type
is usually omitted; if present, every `return` in `main` must supply a value,
which is discarded when the program ends.

Top-level declarations are separated by semicolons where the grammar requires
them; function and control-flow bodies use braces. The whole program is one
source file: there is no import or include mechanism.

## Lesson 3 — Lexical rules

### Identifiers

An identifier starts with an ASCII letter or underscore and continues with
ASCII letters, digits, or underscores. Identifiers are case-sensitive:
`counter`, `_temp`, `draw`, and `main` are examples.

Keywords cannot be used as identifiers. The keywords are:

```text
const var func preimage
if else while do for switch case default
halt return break continue
true false as auto
```

Most type names (`byte`, `word`, `ptr`) are ordinary identifiers in the lexer,
but the parser treats them specially in type position; `as` and `auto` are
keywords. Avoid type names as variable names.

### Comments

Comment forms:

```text
// a line comment

/* a block comment
   that may span several lines */
```

Block comments do not nest. An unterminated block comment is an error.

### Integer literals

Integer literal bases:

```text
123        // decimal
0x7B       // hexadecimal
0b1111011  // binary
0o173      // octal
```

Integer literals must fit into 32 bits; larger literals are rejected.

### Character literals

A character literal is written in single quotes and has type `byte`:

```text
'A'
'\n'
'\x41'
```

The escape sequences are `\a`, `\b`, `\t`, `\n`, `\v`, `\f`, `\r`, `\\`,
`\'`, `\"`, `\0`, and `\xHH`, where `HH` is exactly two hexadecimal digits.
A non-ASCII character is converted from UTF-8 to cp1251. A character that
cannot be represented in cp1251 is an error.

### String literals

A string literal is written in double quotes:

```text
"hello"
"line one\nline two"
"\x41\x42"
```

Strings use the same escapes; non-ASCII characters are converted from UTF-8 to
cp1251. A string literal has type `ptr<byte>`, pointing to the first byte of a
NUL-terminated sequence. Its bytes and terminating zero are stored in the
program; identical literals may share storage.

String literals cannot contain a raw newline, and adjacent literals are not
concatenated automatically.

---

Guide index: [The Strella Language](../../STRELLA.md).

Next: [Values and expressions](02-values.md).

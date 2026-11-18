# Control flow

## Lesson 7 — Control flow

Strella has `if`, `while`, `do`-`while`, `for`, and `switch`, plus `break`,
`continue`, `return`, and `halt`.

### Blocks

A block is a sequence of statements enclosed in braces:

```text
{
  put('A');
  put('B');
}
```

A block is also a scope: its declarations are not visible outside.

### `if`

```text
if (condition) statement
if (condition) statement else statement
```

The condition is an integer, `bool`, or pointer; nonzero/non-null is true.
`else` is optional; there is no `else if` syntax, but an `if` can be the `else`
body:

```text
if (x == 0) {
  put('0');
} else if (x == 1) {
  put('1');
} else {
  put('?');
}
```

### `while`

```text
while (condition) statement
```

The condition (integer, `bool`, or pointer) is tested before each iteration;
false skips the body.

```text
var i: byte = 0;
while (i < 5) {
  put('0' + i);
  i = i + 1;
}
```

### `do`-`while`

```text
do statement while (condition);
```

The body runs once before the test, so it always executes at least once; the
trailing semicolon is required.

```text
var i: byte = 0;
do {
  put('A');
  i = i + 1;
} while (i < 3);
```

### `for`

```text
for (initializer; condition; iteration) statement
```

The initializer may be a declaration, expression, or empty; condition and
iteration may also be empty. A nonempty condition follows the `if` rules.

```text
for (var i: byte = 0; i < 5; i = i + 1) {
  put('0' + i);
}
```

A declaration in the initializer is visible in the condition, the iteration,
and the body, but not after the loop.

### `switch`

```text
switch (value) {
  case constant:
    statements
  case constant:
  case constant:
    statements
  default:
    statements
}
```

The switch value must be an integer or `bool`. Every `case` value must be a
compile-time constant of integer or `bool` type, and duplicate values are
rejected.

Consecutive `case` labels with no statements between them share one body:

```text
switch (x) {
  case 1:
  case 2:
    put('A');
    break;
  default:
    put('B');
}
```

Cases fall through unless `break` executes, as in C. `default` is optional;
with no match and no default, execution continues after the switch.

### `break` and `continue`

`break` exits the nearest enclosing loop or switch. `continue` starts the next
iteration of the nearest enclosing loop; it does not apply to a switch. Using
`break` outside a loop or switch is an error, and using `continue` outside a
loop is an error.

### `return`

```text
return;
return expression;
```

Void functions use `return;` or fall off the end. Non-void functions must
supply an assignable expression in every `return`. Reaching
the end of a non-void function without a `return` returns zero; see
[Lesson 8](04-functions.md).

In `main`, `return` ends the program; any return value is evaluated and
discarded.

### `halt`

```text
halt;
```

`halt` stops execution immediately. Code after it in the same block is
unreachable and may be removed by the optimizer.

---

Guide index: [The Strella Language](../../STRELLA.md).

Next: [Functions](04-functions.md).

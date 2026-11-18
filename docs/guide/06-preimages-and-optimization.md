# Preimages and optimization

## Lesson 13 — Preimages

A `preimage` attaches a startup image; at most one declaration is allowed per project.

```text
preimage: "splash.png";
```

The path is a string literal. It may point to:

- a PNG file that is exactly 16×16 pixels;
- a raw binary file that contains exactly 32 or 64 bytes.

PNG images are converted to color display format; raw files are copied byte for
byte (32 bytes fill the first half, 64 fill the whole display). A preimage
enables the color display at startup.

The image is part of the program, shown at startup with no loading code.

Missing, wrongly sized, or undecodable files fail compilation.

A preimage occupies the display, so a program that uses one should not draw
over the same area unless that is intended.

Example:

```text
preimage: "title.png";

func main() {
  set_devices(display | terminal);
  puts("ready");
}
```

### Display-protect

By default the compiler writes a **display-protected** image.  It checks bits 4
and 5 (`0x30`) of the 63rd non-zero byte of the unprotected image; if either
bit is set, that byte becomes a `nop`.  Otherwise the image is unchanged.

Disable it with:

```text
strella --no-display-protect program.strella
```

Library callers pass `displayProtect = false` to `strella::compile` or
`strella::compileFile`.  Both default to `displayProtect = true`.

## Lesson 14 — Constant folding and the optimizer

An integer expression is a compile-time constant if it uses only integer and
character literals, `true`/`false`, built-in or folded constants, and operators
evaluable without running the program.

For example:

```text
const MASK: byte = 1 << 4;
const VALUE: byte = MASK | 3;

func main() {
  put(1 + 2 + 3 + 4);
}
```

`MASK` is 16, `VALUE` is 19, and the argument to `put` is 10, all computed at compile time.

Constant folding also applies to:

- unary operators on constants;
- comparisons of constants;
- logical operators on constants;
- the conditional operator when the condition and both branches are constant;
- shifts of constants;
- `case` labels, which must be compile-time constants.

As described in [Lesson 5](02-values.md), a folded `const` is replaced by its
value everywhere and occupies no storage; a `const` with a non-constant
initializer is a read-only variable initialized at run time.

### The optimizer

When optimizations are enabled, the compiler also simplifies the program
structure:

- constant subexpressions are replaced by literals;
- `if` statements with a constant condition are replaced by the selected
  branch;
- `while` loops with a constant false condition are removed;
- `do`-`while` loops with a constant false condition are replaced by their
  body;
- statements after `return`, `halt`, `break`, or `continue` in the same block
  are removed.

A `for` with a constant-false condition is kept because its initializer must
run. There is no general algebraic simplification, loop unrolling, or
common-subexpression elimination.

The optimizer is optional: disabling it changes structure, not meaning.
Constant expressions are still evaluated during semantic analysis.

---

Guide index: [The Strella Language](../../STRELLA.md).

Next: [Pointers and strings](07-pointers-and-strings.md).

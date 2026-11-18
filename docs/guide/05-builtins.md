# Built-in functions and constants

## Lesson 11 — Built-in functions

Built-ins are always available, called like ordinary functions but with fixed
signatures and special effects. Their names are reserved in call position; a
user function cannot replace them.

Unless noted, integer parameters accept any integer type, converting to the
parameter's size; `bool` parameters accept any integer (zero false, nonzero
true). `puts` requires exactly the shown type.

### Output to the terminal

```text
puts(ptr<byte> text)
put(byte value)
put(byte a, byte b, byte c, byte d, byte e, byte f)
```

`puts` writes a NUL-terminated string, stopping at the first zero byte. Its
argument must be `ptr<byte>`; a `byte[N]` array decays to that. Bare `ptr` and
other pointer element types are rejected.

```text
set_devices(terminal);
puts("hello");
```

`put` with one argument writes that byte to the terminal as a character. `put`
with six arguments writes one six-byte graphics character.

```text
put('A');
put(1, 2, 3, 4, 5, 6);
```

These are distinct operations; no two- to five-argument form exists.

Output requires enabling the terminal via `set_devices(terminal)` or a mask including it.

### Display

```text
draw(byte index, byte value)
```

`draw` stores `value` unchanged at display memory address `0x40 + index`. The
display must be enabled via `set_devices` for the change to be visible.

```text
set_devices(display);
draw(0, 0x01);
draw(1, 0x02);
```

Display memory is a raw byte buffer; the color display packs four 2-bit pixels
per byte, so a value's meaning depends on the mode. Memory is always reserved,
even without `set_devices`, since the display can be enabled at run time.

### Indicator

```text
indicator(word value)
indicator(sword value)
```

`indicator` sends two bytes: low to port `0x3A`, high to `0x3B`. Any integer or
`bool` is widened to 16 bits: unsigned/`bool` zero-extended, `sbyte`
sign-extended. Enable it with `set_devices(indicator)` (unsigned) or
`set_devices(indicator_signed)` (signed).

```text
set_devices(indicator);
indicator(42);
```

```text
set_devices(indicator_signed);
indicator(-5);
```

Unsigned mode displays a non-negative number; signed mode interprets the value as signed.

### Input

```text
getchar() byte
```

`getchar` returns the cp1251 code of one keyboard character; if none is
available the result is unspecified. Reading consumes the character, so the
next call waits for a new one.

```text
var key: byte = getchar();
put(key);
```

### Device control

```text
set_devices(byte value)
```

`set_devices` configures enabled devices and their behavior using built-in
constants joined with `|`.

```text
set_devices(display | terminal);
set_devices(display_mono | indicator);
set_devices(indicator_signed);
```

The constants are described in the next lesson.

### Random values

```text
random() byte
```

`random` returns a pseudo-random byte; the sequence is unspecified and not reproducible.

```text
var value: byte = random();
```

### Compile-time queries

```text
sizeof(type) byte
len(array) byte
len("text") byte
hi(value) byte
lo(value) byte
```

`sizeof` takes a type, not an expression. It returns the size in bytes: 1 for
byte-sized types, 2 for word-sized types and pointers, the total byte count for
an array type, and the packed size of a structure (the sum of its field sizes).
`len` returns the declared length of an array or the number of characters in a
string literal. `hi` and `lo` return the high and low byte of an integer or
pointer; for a byte-sized argument `hi` is zero and `lo` is the value.

These functions are constant expressions whenever their result is known at
compile time, so they work in `const` initializers, `case` labels, and
`static_assert` conditions:

```text
struct Point { byte x; word y; };
static_assert(sizeof(word) == 2, "word size");
static_assert(sizeof(Point) == 3, "packed Point size");
const palette: byte[3] = [1, 2, 3];
static_assert(len(palette) == 3, "palette length");
var high: byte = hi(0x1234);  // 0x12
var low: byte = lo(0x1234);   // 0x34
```

### Byte rotation

```text
rol8(byte value) byte
ror8(byte value) byte
```

`rol8` and `ror8` rotate the low eight bits of an integer one position left or
right. The result is a byte; bits shifted out reappear at the other end.

```text
var x: byte = 0x81;
put(rol8(x));  // 0x03
put(ror8(x));  // 0xC0
```

Constant arguments are folded; non-constant arguments use the machine's
`shl`/`shr` and `rcl`/`rcr` instructions.

## Lesson 12 — Built-in constants

The compiler predefines five byte constants for device configuration:

| Constant | Value | Meaning |
| --- | --- | --- |
| `display` | 48 | color display |
| `display_mono` | 16 | monochrome display |
| `terminal` | 1 | terminal |
| `indicator` | 4 | unsigned indicator |
| `indicator_signed` | 12 | signed indicator |

These ordinary `byte` constants work in constant expressions:

```text
const output_devices: byte = display | terminal;
```

They can be combined directly in a call:

```text
set_devices(display | terminal);
```

Declared in an outer scope, they can be shadowed by globals or locals, which
may hurt readability.

The values are bit flags: `|` enables several devices. Choose one display mode
and one indicator mode; combining modes with `|` is meaningless.

---

Guide index: [The Strella Language](../../STRELLA.md).

Next: [Preimages and optimization](06-preimages-and-optimization.md).

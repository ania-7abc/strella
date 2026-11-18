# Pointers, strings, and run-time behavior

## Lesson 15 — Pointers, arrays, and strings

A pointer is a 16-bit address written `ptr<T>`; a bare `ptr` is untyped. An
array is declared with a sized type `T[N]`, reserves storage for its elements,
and behaves like a `ptr<T>`.

```text
var message: ptr<byte> = "hello";
const empty: ptr<byte> = "";
var values: byte[4];
var table: ptr<byte>[8];
```

### Address-of and dereference

`&` takes an address, `*` reads a value at an address:

```text
var value: byte = 42;
var p: ptr<byte> = &value;

*p = 7;        // value is now 7
put(*p);       // prints 7
```

`&name` has type `ptr<T>` for a `T` lvalue (variable, array element, or
dereferenced pointer); `&5`, `&(a + b)`, and `&` of a `const` are rejected.
Dereferencing `ptr<T>` yields an assignable `T` lvalue; bare `ptr` yields
`byte`.

A pointer to a structure is written `ptr<Struct>`. Its fields are selected with
`p->field`, or with `p.field`, which automatically dereferences the pointer:

```text
var p: ptr<Pair> = &pair;
p->a = 5;
put(p.b);
```

### Indexing

An array or pointer is indexed with `[]`:

```text
var values: byte[4];
values[0] = 10;
values[3] = values[0] + 5;

var p: ptr<byte> = values;
p[1] = 20;

var words: word[2];
words[1] = 1000;
```

Indexes start at zero and scale by element size, so `words[1]` is two bytes
after `words[0]`. No bounds checking: an out-of-range index can hit unrelated
memory, including code. Elements are lvalues; use `p[index]` rather than
pointer arithmetic.

### Arrays

An array allocates storage for all elements and points to the first; its name
decays to `ptr<T>`, so it can be assigned, passed, returned, and compared as a
pointer.

```text
var values: byte[3];
var p: ptr<byte> = values;

values[0] = 1;
p[1] = 2;
put(values[1]);   // prints 2
```

The array rules from [Lesson 5](02-values.md) apply: an array cannot be `const`
or have an initializer, must have a positive literal length, and cannot be
unsized or multi-dimensional; `auto` deduces a pointer. An array parameter or
return type is treated as the corresponding pointer type, and the declared
length is not part of the function type or checked.

### Explicit pointer conversions

A pointer can be converted explicitly with `as`, using the conversions listed
in Lesson 4:

```text
var raw: word = p as word;
var generic: ptr = p as ptr;
var typed: ptr<byte> = generic as ptr<byte>;
var again: ptr<byte> = raw as ptr<byte>;
```

Pointer ↔ word-sized integer conversion uses the address as a 16-bit value;
`byte`/`sbyte` need a wider intermediate. The compiler does not check converted
addresses for validity.

### Comparing pointers

Comparisons use addresses, not pointed-to bytes; identical string literals may
share storage, so pointer equality is not string equality.

Pointers convert implicitly to `bool` (null is false), making `if (p)` a null
check. With no null literal, use `0 as word as ptr<T>`.

### Strings

A string literal is a NUL-terminated `ptr<byte>`; `puts` stops at the first
zero, so an embedded `\0` truncates output.

Strings can be indexed like any `ptr<byte>`:

```text
put("hello"[1]);   // prints e
```

Length is not stored at run time. Do not write through a string-literal
pointer; copy the string into an array to edit it.

To print a string, enable the terminal first:

```text
set_devices(terminal);
puts("hello");
```

## Lesson 16 — Compile-time and run-time behavior

Values are either compile-time constants or run-time values.

### Compile-time constants

An integer expression is a compile-time constant when it is built only from:

- integer and character literals;
- `true` and `false`;
- built-in constants;
- constants whose own initializers are compile-time constants;
- operators applied to compile-time constants, including explicit `as`
  conversions.

A string literal is a constant pointer whose value is its address: storable in
`const`, comparable, and convertible with folded `as`, but not usable in
integer arithmetic.

A const with a constant initializer is folded (no storage, replaced at every
use) in arithmetic, bitwise, comparison, logical, shift, conditional, unary,
and `as` expressions.

```text
const BASE: byte = 10;
const STEP: byte = 1 << 2;
const LIMIT: byte = BASE + STEP;
```

`BASE` = 10, `STEP` = 4, `LIMIT` = 14; none needs storage.

A const with a non-constant initializer is a read-only variable, initialized at
startup (global) or each time its declaration executes (local), and never
assignable.

### Run-time values

A `var` always creates storage; its initializer runs at startup (global) or at
the declaration (local).

Globals with constant initializers start with those values; computed ones are
initialized at startup in declaration order; uninitialized ones start as zero.

Arrays reserve element storage and point to the first element. Elements are
not initialized by the declaration: global arrays start as zero, local arrays
may hold old values, so assign every element whose first value matters.

A local pointer is not automatically null; initialize any local whose first
value matters, since its storage may be reused from an earlier call.

### Evaluation order

Binary operands and call arguments are evaluated left to right. Assignment
evaluates the right side, then the left address, then stores; indexing evaluates
the base before the index.

`&&`/`||` short-circuit; `?:` evaluates the condition and exactly one branch.

Conditions are integers, `bool`, or pointers: zero/null is false, everything
else true. Conversion to `bool`, implicit or via `as`, yields exactly `0` or
`1`.

Do not rely on any other ordering of side effects.

---

Guide index: [The Strella Language](../../STRELLA.md).

Next: [Grammar and diagnostics](08-grammar-and-diagnostics.md).

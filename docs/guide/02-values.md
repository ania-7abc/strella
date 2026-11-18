# Values and expressions

## Lesson 4 — Types and values

Every expression has a type. The available types are:

| Type | Size | Meaning |
| --- | --- | --- |
| `byte` | 1 byte | unsigned integer, 0 to 255 |
| `sbyte` | 1 byte | signed integer, −128 to 127 |
| `word` | 2 bytes | unsigned integer, 0 to 65535 |
| `sword` | 2 bytes | signed integer, −32768 to 32767 |
| `bool` | 1 byte | logical value: `true` is 1, `false` is 0 |
| `ptr` | 2 bytes | untyped pointer; its elements are bytes |
| `ptr<T>` | 2 bytes | pointer to a value of type `T` |
| `T[N]` | 2 bytes | array of `N` elements of type `T`; behaves like `ptr<T>` |
| `Name` | sum of fields | a packed structure declared with `struct` |
| `auto` | — | placeholder type deduced from an initializer |
| `void` | 0 bytes | the absence of a value; used for function returns |

Pointers are 16-bit addresses: `ptr<byte>` points to a byte, `ptr<ptr<byte>>`
to a pointer to a byte, and bare `ptr` holds any address whose elements are
treated as `byte` when dereferenced or indexed.

An array type is `T[N]`, where `N` is a positive integer literal. Its
declaration reserves `N` elements of `T` and initializes a pointer to the
first. An array value is interchangeable with `ptr<T>`: assignable, passable,
and returnable as `ptr<T>`, and indexable. `T[]` and `T[N][M]` are rejected,
the length is not stored at run time, and indexing is not bounds-checked.

`bool` behaves as an unsigned one-byte integer in arithmetic and conversions;
comparisons, logical operators, and `true`/`false` produce it.

### Type aliases

A type alias gives an existing type another name:

```text
type Addr = ptr<byte>;
type Buffer = byte[16];
```

Aliases are declared at the top level, must appear before their first use, and
can be used anywhere a type is expected, including inside `ptr<...>` and array
types. They add no runtime cost and do not create a distinct type.

### Structures

A structure groups named fields under a new type name:

```text
struct Point {
  byte x;
  word y;
};
```

The declaration must appear at the top level before the structure is used.
Field types may be scalar, pointer, array, or previously declared structure
types. They are laid out **packed**: each field starts immediately after the
previous one, with no padding or alignment. `sizeof(Point)` in the example is
3. Field order is preserved, and a structure is at most 255 bytes.

Structures are nominal: two structures with identical fields are still distinct
types. A structure cannot contain itself by value, but it can contain a pointer
to itself. Field names must be unique within a structure.

Variables and constants of a structure type are declared like other values:

```text
var p: Point;
const origin: Point = {0, 0};
```

A braced literal is assigned positionally, one element per field. Constant
literals are stored in the image; runtime literals are copied into storage.
Nested literals use the same syntax:

```text
struct Pair {
  byte a;
  byte b;
};
struct Tagged {
  byte tag;
  Pair pair;
};
var item: Tagged = {7, {1, 2}};
```

Assigning a structure copies all its bytes. Structures can also be used as
function parameters and return values, and in arrays (`Pair[4]`) or through
pointers (`ptr<Pair>`).

### Implicit conversions

Automatic conversions:

- every non-`void` value converts to `bool`: zero, a null pointer, and `false`
  become `false`; every other value becomes `true`;
- `bool` widens to any integer type;
- `byte` widens to `word` or `sword`;
- `sbyte` widens to `sword`;
- a compile-time `byte` value in `0..127` converts implicitly to `sbyte`;
- an array type decays to `ptr<T>` when it is stored, passed, returned, or
  used where a pointer is expected.

Conversions to `bool` yield exactly `0` or `1`. Apart from the constant
`byte` → `sbyte` conversion above, there is no implicit narrowing (`word` to
`byte`), signedness change, or integer-to-pointer conversion. A runtime `byte`
or a byte constant above `127` still needs an explicit `as sbyte`.

### Explicit conversions

Explicit conversions use `as`:

```text
value as type
```

The `as` operator is allowed for:

- any integer type to any integer type;
- any pointer type to any pointer type, including `ptr<T>` to `ptr` and back;
- a pointer to a word-sized integer type (`word` or `sword`);
- a word-sized integer type to a pointer.

Narrowing keeps the low bits. Pointer ↔ word-sized integer conversions use the
16-bit address; `byte`/`sbyte` require a `word` intermediate. The target may
not be `auto`, an array type, or `void`.

A string literal is a `ptr<byte>`, usable as such and explicitly convertible
to other pointer types or `word`.

### Literal typing

An integer literal ≤ 255 has type `byte`; larger ones are `word`. `true` and
`false` are `bool`, character literals are `byte`, string literals are
`ptr<byte>`.

### The common type of an operation

For binary arithmetic or bitwise operators on two integers:

- if either operand is a word-sized type, the result is `word` or `sword`;
- otherwise the result is `byte` or `sbyte`;
- the result is signed if either operand is signed;
- `bool` counts as an unsigned byte.

For example, `byte + word` produces `word`, `byte + sbyte` produces `sbyte`,
and `bool + bool` produces `byte`.

## Lesson 5 — Variables, constants, and scope

A variable or constant is declared with `var` or `const`:

```text
var name: type;
var name: type = expression;
var name: auto = expression;
const name: type = expression;
const name: auto = expression;
```

The type is normally explicit. An initializer is optional for `var` and
mandatory for `const` (and for `auto`). `auto` deduces the initializer's type,
with array decay to a pointer; it is not allowed for parameters, return types,
array element types, or pointer element types.

Examples:

```text
var count: byte;
var total: word = 1000;
var copy: auto = total;        // word
var address: auto = &count;    // ptr<byte>
const LIMIT: byte = 10;
const MASK: byte = 0b00001111;
const NAME: auto = "strella";  // ptr<byte>
```

Numeric literals cannot contain underscores: write `0b00001111`, not `0b0000_1111`.

### Arrays

An array is declared with a sized type:

```text
var values: byte[4];
var addresses: ptr<byte>[8];
var row: word[3];
```

Elements can also be supplied with an array literal of compile-time constants:

```text
const palette: byte[3] = [10, 20, 30];
var row: word[2] = [0x1234, 0x5678];
```

A literal used as an expression yields a pointer to data placed in the memory
image. Its element type is inferred from the elements (the common integer type),
and each element must be a constant that fits that type. When used as an
initializer, the declared element type is used and the element count must match
the declared length exactly.

```text
var values: byte[3] = [10, 20, 30];
values[0] = 11;
```

Arrays in `@inline` or `@stack` functions cannot have initializers. The length
must be a positive integer literal. Unsized arrays (`T[]`), zero-length arrays,
and multi-dimensional arrays are rejected.

### Constants

A `const` is read-only; assignment to it is an error.

A compile-time-constant initializer is folded: the name has no storage and
every use is replaced by its value. Folded constants can appear in other
constant expressions, and integer ones in `case` labels.

Otherwise the declaration creates a read-only variable, initialized at startup
(global) or when its declaration executes (local), and still not assignable.
For example:

```text
const answer: word = compute_answer(2, 3);
```

A constant initializer must be assignable to the declared type. A folded
constant value must also fit into the declared type.

### Static assertions

`static_assert` checks a compile-time condition and reports a message if it
fails. It can appear at the top level or inside a block, and generates no code:

```text
static_assert(sizeof(word) == 2, "word must be two bytes");
const palette: byte[3] = [10, 20, 30];
static_assert(len(palette) == 3, "palette length");
```

The condition must be a constant expression; otherwise the compiler reports
`static_assert condition must be a constant expression`.

### Variables

A `var` is writable; its initializer, if any, runs at the declaration (local)
or at startup (global). Without an initializer, no initialization is performed.

Uninitialized globals start as zero. Uninitialized locals may reuse storage
from earlier calls, so initialize any local whose first value matters.

### Global and local declarations

Top-level declarations are global and visible to all functions; declarations
in a function or block are local to that block.

Each block is a new scope, except that a function's outermost block shares the
parameter scope. Inner declarations may shadow outer names; duplicate names in
one scope are an error.

### Declaration statements

A declaration may appear as a statement inside a block:

```text
func main() {
  var a: byte = 1;
  {
    var a: byte = 2;   // shadows the outer a
    put('0' + a);
  }
  put('0' + a);
}
```

The output is the character `2` followed by the character `1`.

A declaration in a `for` initializer is scoped to that loop:

```text
for (var i: byte = 0; i < 10; i = i + 1) {
  put(i);
}
```

The name `i` is not visible after the loop.

## Lesson 6 — Expressions and operators

Expressions have a type and may be used as statements, initializers,
conditions, arguments, and return values.

### Operator summary

The operators are:

```text
+  -  >>  <<  &  |  ^  ~  &&  ||  !  ?:  ()  []  .  ->  =  ==  !=  <  <=  >  >=
*  as  ++  --
```

`&` is bitwise AND (binary) or address-of (unary); `*` dereferences. There is
no multiplication, division, or remainder operator. `()` groups/calls, `[]`
indexes, `.` and `->` select structure fields, `?:` is conditional, `=` assigns,
`==` tests equality, `as` converts, and `++`/`--` increment/decrement.

### Precedence

From highest to lowest, the precedence is:

1. function calls, indexing, field selection with `.`/`->`, postfix `++`/`--`,
   and parenthesized expressions;
2. `as`;
3. unary `!`, `~`, `+`, `-`, `&`, `*`, and prefix `++`/`--`;
4. `+`, `-`;
5. `<<`, `>>`;
6. `<`, `<=`, `>`, `>=`;
7. `==`, `!=`;
8. `&`;
9. `^`;
10. `|`;
11. `&&`;
12. `||`;
13. `?:`;
14. `=`.

Assignment and the `else` branch of `?:` are right-associative (`a = b = 3`
assigns `3` to `b`, then `a`); other binary operators are left-associative.
`as` binds tighter than unary and binary operators: `-x as sword` means
`-(x as sword)`; parenthesize to cast a unary result.

### Arithmetic and bitwise operators

`+` adds, `-` subtracts, and `&`, `|`, `^` are bitwise AND, OR, and XOR.
Operands must be integers or `bool`; the result follows Lesson 4's common-type
rules. Pointers cannot take part in arithmetic; use indexing. Without a
multiplication operator, use repeated addition or a loop.

### Shifts

`<<` and `>>` shift left and right. Both operands must be integers or `bool`;
the result has the left operand's type, except `bool` yields `byte`.

Signed right shift is arithmetic (sign preserved); unsigned right shift fills
with zeros. The right operand is the count, taken modulo 32 for constants at
compile time; at run time bits are shifted one at a time. A count ≥ operand
width gives zero for left/unsigned-right shifts, and zero or all ones for
signed right shifts, following the sign.

### Unary operators

- `!value` is `true` for zero or a null pointer, `false` otherwise; result
  `bool`.
- `~value` inverts every bit; result is the operand type, except `bool` → `byte`.
- `+value` is the identity, with the operand's type.
- `-value` negates: `byte` → `sbyte`, `word` → `sword`; signed operands and
  `bool` keep their type.
- `&value` is address-of, requiring a variable, array element, or dereferenced
  pointer; result `ptr<T>` for a `T` operand, with `&array` equal to the array
  pointer.
- `*pointer` dereferences: `ptr<T>` yields an assignable `T` lvalue, bare `ptr`
  yields `byte`.
- `++value`/`--value` are prefix increment/decrement on writable integer
  lvalues (no `bool` or pointers); result is the new value. Postfix forms follow.

### Indexing

An `array[index]` or `pointer[index]` requires an array/pointer base and an
integer or `bool` index. Indexes start at zero, scale by element size, and are
not bounds-checked. Elements are lvalues: assignable, incrementable, or
passable by address.

```text
var values: byte[4];
values[0] = 10;
values[3] = values[0] + 5;

var p: ptr<byte> = values;
p[1] = 20;
```

A bare `ptr` indexes bytes. Indexing a `ptr<word>` advances two bytes per
index. There is no pointer arithmetic: `p + 1` is not allowed, but `p[1]` is.

### Structure fields

A field is selected with `.` after a structure value. For a pointer to a
structure, both `p.field` and `p->field` select the field; `.` automatically
dereferences the pointer:

```text
struct Pair {
  byte a;
  byte b;
};

var p: Pair = {1, 2};
put(p.a);          // 1
p.b = 9;

var q: ptr<Pair> = &p;
put(q->a);         // 1
put(q.b);          // 9; same field as q->b
```

Fields are lvalues: they can be assigned, incremented, passed by address, and
used wherever their field type is expected. Field access requires a complete
structure type and an existing field name. Structures do not participate in
arithmetic or comparison; compare or operate on their fields instead.

### Increment and decrement

`++`/`--` add/subtract one on writable integer lvalues (not `bool` or
pointers). Prefix yields the new value, postfix the old; overflow wraps modulo
the type's width.

```text
var count: byte = 0;
count++;          // count is 1
++count;          // count is 2
var old: byte = count--;  // old is 2, count is 1
```

### Explicit conversions

`expression as type` performs a non-implicit conversion; the allowed ones are
listed in Lesson 4. Casts bind tighter than binary operators and chain left to
right:

```text
var raw: word = pointer as word;
var back: ptr<byte> = raw as ptr<byte>;
```

### Comparisons

Comparisons are `==`, `!=`, `<`, `<=`, `>`, `>=`; operands must both be integers
or both pointers, result `bool`. Arrays decay to pointers. Ordered comparisons
are signed if either operand is signed; `==`/`!=` compare bit patterns. Any
comparison operator works on two pointers.

### Logical operators

`&&` and `||` require integer, `bool`, or pointer operands and yield `bool`.
Both short-circuit: `b` in `a && b` runs only when `a` is nonzero/non-null; in
`a || b` only when `a` is zero/null. The result is exactly `0` or `1`.

### The conditional operator

The conditional operator has the form:

```text
condition ? then_value : else_value
```

The condition is an integer, `bool`, or pointer (zero/null is false); branches
must both be integers or both pointers, yielding the common integer type or the
pointer type. Arrays decay; only the selected branch is evaluated.

Example:

```text
const bigger: byte = a > b ? a : b;
```

### Assignment

Assignment has the form:

```text
lvalue = expression
```

The left side must be a writable lvalue: variable, array element, structure
field, or dereferenced pointer — not a constant, call, or arbitrary expression.
The right side must be assignable using Lesson 4's implicit conversions. An
assignment's value is the assigned value, so assignments chain.

Assignment is a statement when followed by a semicolon:

```text
count = count + 1;
values[0] = 42;
*pointer = 7;
```

It is an expression when used inside a larger expression:

```text
a = b = 3;
```

### Function calls

A function call is written as a name followed by a parenthesized argument list:

```text
add(2, 3)
random()
```

Only named functions can be called; there are no function pointers or anonymous
functions. Argument count and types are checked against the declaration;
arguments must be assignable to parameters, with array decay. Built-ins are
described in [Lesson 11](05-builtins.md).

---

Guide index: [The Strella Language](../../STRELLA.md).

Next: [Control flow](03-control-flow.md).

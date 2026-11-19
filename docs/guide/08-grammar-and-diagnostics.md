# Grammar, diagnostics, and a complete program

## Lesson 17 — The complete grammar

Notation: `*` = zero or more, `?` = optional, `|` = choice, parentheses group
alternatives; uppercase names are tokens, lowercase are grammar rules.
Whitespace and comments may appear between tokens.

### Program structure

```text
program      := topLevel*
topLevel     := preimage | structDecl | typeAlias | staticAssert
               | globalDecl | functionDecl

preimage     := "preimage" ":" STRING ";"
structDecl   := "struct" IDENT "{" field* "}" ";"
field        := type IDENT ";"
typeAlias    := "type" IDENT "=" type ";"
globalDecl   := ("const" | "var") IDENT ":" type ("=" expr)? ";"
staticAssert := "static_assert" "(" expr "," STRING ")" ";"

functionDecl := annotation? "func" IDENT "(" parameters? ")" type? block
annotation   := "@" ("inline" | "stack")
parameters   := parameter ("," parameter)*
parameter    := type IDENT

type         := ("byte" | "sbyte" | "word" | "sword" | "bool" | "auto"
                | "ptr" ("<" type ">")? | IDENT) ("[" INT "]")?
```

A type is a built-in scalar, `auto`, pointer, previously declared alias, or
declared structure name, optionally with one array suffix (one dimension,
positive literal length). Structure names are declared at the top level before
use; their fields use the same `type` rule. There is exactly one `main` with no
parameters and at most one `preimage`. `void` is implicit: no written return
type means `void`.

### Statements

```text
statement    := block | declaration | staticAssert | ifStmt | whileStmt
              | doWhileStmt | forStmt | switchStmt | breakStmt
              | continueStmt | returnStmt | haltStmt | exprStmt | ";"

block        := "{" statement* "}"
declaration  := ("const" | "var") IDENT ":" type ("=" expr)? ";"

ifStmt       := "if" "(" expr ")" statement ("else" statement)?
whileStmt    := "while" "(" expr ")" statement
doWhileStmt  := "do" statement "while" "(" expr ")" ";"

forStmt      := "for" "(" (declaration | expr ";" | ";")
                expr? ";" expr? ")" statement

switchStmt   := "switch" "(" expr ")" "{" switchCase* "}"
switchCase   := ("case" expr ":" | "default" ":") statement*

breakStmt    := "break" ";"
continueStmt := "continue" ";"
returnStmt   := "return" expr? ";"
haltStmt     := "halt" ";"
exprStmt     := expr ";"
```

### Expressions

```text
expr        := assignment
assignment  := ternary ("=" assignment)?
ternary     := logicalOr ("?" expr ":" ternary)?
logicalOr   := logicalAnd ("||" logicalAnd)*
logicalAnd  := bitwiseOr ("&&" bitwiseOr)*
bitwiseOr   := bitwiseXor ("|" bitwiseXor)*
bitwiseXor  := bitwiseAnd ("^" bitwiseAnd)*
bitwiseAnd  := equality ("&" equality)*
equality    := relational (("==" | "!=") relational)*
relational  := shift (("<" | "<=" | ">" | ">=") shift)*
shift       := additive (("<<" | ">>") additive)*
additive    := unary (("+" | "-") unary)*
unary       := ("!" | "~" | "+" | "-" | "&" | "*" | "++" | "--") unary
             | cast
cast        := postfix ("as" type)*
postfix     := primary (callSuffix | indexSuffix | fieldSuffix | incDecSuffix)*
callSuffix  := "(" arguments? ")"
indexSuffix := "[" expr "]"
fieldSuffix := ("." | "->") IDENT
incDecSuffix := "++" | "--"
primary     := INT | CHAR | STRING | "true" | "false" | IDENT
             | "(" expr ")" | arrayLiteral | structLiteral | sizeofExpr
arrayLiteral := "[" (expr ("," expr)* ","?)? "]"
structLiteral := "{" (expr ("," expr)* ","?)? "}"
sizeofExpr  := "sizeof" "(" type ")"
arguments   := expr ("," expr)*
```

Only identifiers can be called. `=` requires a writable lvalue, not necessarily
a plain name. Binary operators are left-associative except right-associative
`=` and the `?:` else
branch; `as` binds tighter than unary operators (`-x as sword` is `-(x as
sword)`) and chains.

### Tokens

```text
IDENT   := [A-Za-z_][A-Za-z0-9_]*
INT     := decimal | "0x" hex | "0b" binary | "0o" octal
CHAR    := "'" character "'"
STRING  := '"' character* '"'
```

The base prefixes `0x`, `0b`, and `0o` may be written with an uppercase letter.
An octal literal uses the `0o` prefix, as described in
[Lesson 3](01-overview.md).

The keywords are listed in [Lesson 3](01-overview.md). Most type names, plus
`type` and `static_assert`, are ordinary identifiers recognized only in
declaration position; `auto` and `as` are keywords.

### Rules not expressed by the grammar

The grammar accepts some programs that are rejected by semantic analysis:

- a program must have exactly one `main` function, and `main` must have no
  parameters;
- a program may have at most one `preimage` declaration;
- a name may not be declared twice in the same scope;
- every name and every called function must be declared;
- types must match, except for the implicit conversions and array decay
  described in [Lesson 4](02-values.md); in particular, only a compile-time
  `byte` value in `0..127` changes implicitly to `sbyte`, while a runtime byte
  or a larger constant requires `as sbyte`;
- an array declaration must have a positive literal length; its optional
  initializer must be a non-empty array literal with one constant element per
  declared slot; unsized and multi-dimensional arrays are rejected; arrays in
  `@inline` and `@stack` functions cannot have initializers;
- a structure declaration must precede its first use; field names must be unique,
  field types must have a size and may not be `auto`, `void`, or an incomplete
  structure by value, and the packed size must be at most 255 bytes;
- a braced struct literal needs a target structure type and exactly one
  assignable element per field; a constant struct needs a constant initializer;
- structure types are nominal: two distinct declarations are not interchangeable,
  even with identical fields;
- a type alias must precede its first use and may not rename a built-in type;
- a `static_assert` condition must be a constant expression;
- `auto` requires an initializer and may not be used as a parameter type, a
  return type, an array element type, or a pointer element type;
- assignment and `++`/`--` require a writable lvalue; `++`/`--` accept only
  integer operands and reject `bool` and pointers;
- `&` requires an lvalue, `*` and `[]` require a pointer or array, and `as`
  accepts only the conversions listed in [Lesson 4](02-values.md);
- `.` requires a structure or a pointer to a structure and an existing field;
  `->` requires a pointer to a structure; `.` automatically dereferences
  structure pointers;
- pointers do not take part in arithmetic; use indexing instead;
- `case` values must be compile-time constants of integer or `bool` type, must
  be unique, and the switch value must be an integer or `bool`;
- `break` must be inside a loop or switch, and `continue` must be inside a
  loop;
- a `return` statement in a non-void function must have a value, and a
  `return` statement in a void function must not;
- a function without `@stack` may call built-in functions and only `@inline`
  and `@stack` user functions, and may not be recursive;
- an integer literal must fit into 32 bits.

## Lesson 18 — Diagnostics and common mistakes

The compiler reports an error with a source location and a short message and
stops at the first error. The table groups the most common mistakes.

| Situation | Meaning and fix |
| --- | --- |
| `unknown identifier` | The name is misspelled or is used outside its scope. Check the spelling and the enclosing block. |
| `unknown function` | The function has not been declared, or a built-in name is misspelled. |
| `duplicate declaration` | The same name is declared twice in one scope. Use a nested block to shadow, or choose another name. |
| `constant needs an initializer` | A `const` declaration must have `= expression`. |
| `cannot assign to a constant` | The left side is a `const`, or a `const` element was indexed. Declare it with `var` if it must change. |
| `cannot modify a constant` | `++` or `--` was applied to a `const`. |
| `cannot initialize ... with ...` | The initializer type is not assignable to the declared type. Use an allowed implicit conversion, or an explicit `as` cast. |
| `cannot assign ... to ...` | The right side is not assignable to the left type. Check the pointer element types and the implicit conversions. |
| `cannot return ...` | The returned value does not match the function's return type. |
| `non-void function must return a value` | A `return` statement in a function with a return type must have an expression. |
| `void function cannot return a value` | A function with no return type must use `return;`, without an expression. |
| `left side of assignment must be a variable or memory location` | The left side is not a writable lvalue. Use a name, an array element, or a dereferenced pointer. |
| `array types require a size; 'T[]' is not supported` | Write `T[N]` with a positive literal length. |
| `array length must be positive` | An array length of zero is not allowed. |
| `multi-dimensional arrays are not supported` | Declare one array per dimension, or use a flat array. |
| `an array initializer must be an array literal` | Use `[value, ...]` with constant elements. |
| `array literal cannot be empty` | Provide at least one constant element. |
| `array literal has N element(s), expected M` | The literal's element count must match the declared array length. |
| `array literal elements must be constant` | Array data is written into the image at compile time; use constant expressions. |
| `an array in an @inline or @stack function cannot have an initializer` | Initialize such arrays with assignments after declaration. |
| `duplicate type name '...'` | A struct or alias reuses a built-in or previously declared type name. Choose another name. |
| `duplicate field '...' in struct '...'` | Field names must be unique within one structure. |
| `incomplete struct '...' cannot be a field by value` | Declare the field type before using it, or store a `ptr<Struct>` to it. |
| `struct '...' is too large` | The packed structure exceeds 255 bytes. Split it or use pointers. |
| `a struct literal needs a target type in this context` | Assign the literal to a typed variable/parameter, or use it where the structure type is known. |
| `struct initializer has N field(s), expected M` | Supply exactly one value per field, in declaration order. |
| `struct '...' has no field '...'` | Check the field name and the structure type. |
| `field access expects a struct, got ...` | `.` and `->` work on structures, not scalars or arrays. |
| `'->' expects a struct pointer` | Use `->` with `ptr<Struct>`; use `.` with a structure value. |
| `a constant struct needs a constant initializer` | Initialize a `const` structure with a constant braced literal. |
| `duplicate type alias` / `cannot redefine a built-in type name` | Choose another alias name. |
| `static_assert condition must be a constant expression` | Use literals, folded constants, and compile-time queries only. |
| `static assertion failed: ...` | The condition was false; the message is the one supplied in the assertion. |
| `'auto' needs an initializer to deduce the type` | `auto` has no type of its own; provide `= expression`. |
| `a parameter type cannot be 'auto'` | Write the parameter type explicitly. |
| `a function return type cannot be 'auto'` | Write the return type explicitly. |
| `auto cannot be used as an array element type` | Write the element type explicitly. |
| `auto cannot be used as a pointer element type` | Write the pointed-to type explicitly, or use bare `ptr` for an untyped pointer. |
| `cannot deduce a type from ...` | The initializer has no usable value type, such as `void`. |
| `cannot convert ... with 'as'` | The requested conversion is not allowed. See the explicit conversions in [Lesson 4](02-values.md). |
| `invalid cast target type ...` | `as` cannot target `auto`, an array type, or `void`. |
| `indexing expects an array or pointer, got ...` | The base of `[]` must be an array or pointer. |
| `index must be an integer, got ...` | The index must be an integer or `bool`. |
| `address-of expects a variable or memory location` | `&` needs a variable, an array element, or a dereferenced pointer. |
| `cannot take the address of a constant` | A folded `const` has no storage and no address. |
| `'*' expects a pointer` | Dereference needs a pointer operand. |
| `'puts' expects ptr<byte>` | `puts` accepts only a `ptr<byte>` or a `byte[N]` array. Cast other pointers explicitly. |
| `indicator argument must be an integer` | `indicator` accepts integers and `bool`, not pointers or other values. |
| `arithmetic operator expects integer operands, got ...` | Pointers do not support arithmetic; use indexing. |
| `... expects an integer variable, got ...` | `++` and `--` accept only non-`bool` integer lvalues. |
| `comparison expects two integers or two pointers` | Both sides must be integers or both must be pointers. |
| `condition must be an integer, bool or pointer` | A condition may not be a `void` value or an incompatible type. |
| `case value must be a compile-time constant` | A `case` label cannot depend on a run-time value. |
| `duplicate case value` | Two `case` labels have the same value. |
| `break outside of a loop or switch` | `break` needs an enclosing loop or switch. |
| `continue outside of a loop` | `continue` needs an enclosing loop. |
| `... can only call @inline and @stack functions` | A static or inline function called a static function. Mark the callee `@inline` or `@stack`, or mark the caller `@stack`. |
| `recursive call ... requires every function in the cycle to be @stack` | A recursion cycle contains a static or inline function. Mark every function in the cycle `@stack`. |
| `preimage is not supported on the 32k target` | A preimage declaration was compiled with `--target 32k` (or `Target::K32`); preimages need the 1k target. |
| `preimage must be 32 or 64 bytes` | A raw preimage file has the wrong size. A PNG preimage must be 16×16 pixels. |

When an error points at a declaration, check the declared type first; at an
expression, check the operand types and argument count; at a call, check the
annotation and call rules from [Lessons 8 to 10](04-functions.md).

## Lesson 19 — A complete program

The following program enables the terminal and display, prints a message,
writes display bytes, and computes a sum with a recursive stack function.

```text
const START: byte = 0;
const MESSAGE: ptr<byte> = "strella";

var counter: byte = 0;

@inline func plot(byte index, byte value) {
  draw(index, value);
}

@stack func sum(byte value) byte {
  if (value == 0) {
    return 0;
  }
  return value + sum(value - 1);
}

func main() {
  set_devices(display | terminal);
  puts(MESSAGE);

  var pixels: byte[3];
  pixels[0] = START;
  pixels[1] = START + 1;
  pixels[2] = START + 2;

  var cursor: ptr<byte> = pixels;
  plot(cursor[0], 0x01);
  plot(cursor[1], 0x02);
  plot(cursor[2], 0x03);

  var total: auto = sum(3);
  counter++;

  put('0' + total);
  put('0' + counter);
}
```

`MESSAGE` is a folded `ptr<byte>` constant. `plot` is inline, expanded at each
call, writing through `draw` to display memory; `sum` is a recursive stack
function. `pixels` allocates three bytes and `cursor` points to its first
element, so `cursor[i]` is `pixels[i]`. `total` uses `auto` to deduce `sum`'s
return type, and `counter++` adds one. The program prints `strella`, then `6`
and `1`.

---

Guide index: [The Strella Language](../../STRELLA.md).

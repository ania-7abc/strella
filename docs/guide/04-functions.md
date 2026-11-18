# Functions

## Lesson 8 — Functions

A function is declared with `func`:

```text
func name(type parameter, type parameter, ...) return_type {
  statements
}
```

Omitting the return type yields `void`.

```text
func add(byte a, byte b) byte {
  return a + b;
}

func greet(ptr<byte> text) {
  puts(text);
}
```

A function may have no parameters:

```text
func answer() byte {
  return 42;
}
```

Parameters are comma-separated `type name` pairs, local to the function body.
Arrays are passed as pointers to their first element; structures are passed by
value and copied byte for byte.

### Structure parameters and returns

A complete structure type may be used as a parameter or return type:

```text
struct Pair {
  byte a;
  byte b;
};

@inline func swap(Pair p) Pair {
  var t: byte = p.a;
  p.a = p.b;
  p.b = t;
  return p;
}

func main() {
  var p: Pair = {1, 2};
  var q: Pair = swap(p);   // q is {2, 1}; p is unchanged
  put(q.a);
  put(q.b);
}
```

The parameter is a copy, so modifying it does not change the caller's value. To
modify the caller's structure, pass `ptr<Pair>` instead and use `p->field` (or
`p.field`). Structure return values are copied back to the caller.

A structure-returning function uses one return slot per function. Do not
re-enter such a function before its previous return value has been copied;
in particular, recursive structure returns are not supported.

### `main`

`main` is the entry point: the program starts by executing it. As described in
Lesson 2, it must have no parameters.

```text
func main() {
  put('A');
}
```

### Calling functions

A call uses the function name and a parenthesized argument list:

```text
add(2, 3)
answer()
greet("hello")
```

Argument count must match, and each argument must be assignable to its
parameter. A `void` call cannot be used as a value; a value-returning call can.

### Function annotations

A function declaration may be preceded by `@inline` or `@stack`:

```text
@inline func twice(byte value) byte {
  return value + value;
}

@stack func sum(byte value) byte {
  if (value == 0) {
    return 0;
  }
  return value + sum(value - 1);
}
```

Without an annotation a function is **static** (the default); the three kinds
differ in restrictions and behavior, described in the next two lessons.

### Restrictions for functions without `@stack`

A function that is not marked `@stack` — whether it is static or `@inline` —
has these restrictions:

- it may call built-in functions and only `@inline` and `@stack` user
  functions;
- it may not participate in recursion.

Parameter total size is unlimited. An array parameter is passed as a pointer
to its first element, occupying one pointer regardless of declared length.

Since `main` is static, it may call only `@inline` and `@stack` functions; a
static helper is callable from `@stack` functions but not from `main`, other
static functions, or inline functions.

### Falling off the end

A non-void function that reaches the end of its body without a `return`
produces zero.

## Lesson 9 — Inline functions

`@inline` expands the function at each call site instead of generating a
separate body.

```text
@inline func twice(byte value) byte {
  return value + value;
}

func main() {
  put(twice(3));
}
```

The body behaves as if written at the call site with parameters replaced by
arguments. It can use `return`, `if`, loops, and `switch`; `return` leaves the
expansion, not the caller.

Inlining avoids a call and can shrink programs when a small helper is used a
few times. Parameters and locals stay separate per expansion, even inside
recursive `@stack` functions.

An inline function has the same restrictions as any other non-`@stack`
function: it may call built-in functions and only `@inline` and `@stack` user
functions, and it may not participate in recursion.

An unused inline function has no effect; it does not exist as a separate callable function.

## Lesson 10 — Stack functions and recursion

`@stack` removes the call and recursion restrictions. Such a function:

- may call built-in, static, inline, and stack functions;
- may be recursive;
- may call itself directly or through a cycle of `@stack` functions.

```text
@stack func sum(byte value) byte {
  if (value == 0) {
    return 0;
  }
  return value + sum(value - 1);
}

func main() {
  put(sum(5));
}
```

`sum(5)` produces 15.

Parameters and locals are kept on a stack per call; stack functions are slower
and larger, so use them when recursion or calling a static function is needed.

Every function in a recursive cycle must be `@stack`; a cycle containing a
static or inline function is rejected, preventing re-entry while parameters or
locals are in use.

---

Guide index: [The Strella Language](../../STRELLA.md).

Next: [Built-ins](05-builtins.md).

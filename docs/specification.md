# Gable language specification — version 0.1

Status: **experimental**. This document describes the language implemented by the
`gable` 0.1 compiler. Where the implementation and this document disagree, the
implementation is wrong if the behaviour is unspecified here, and the document
is wrong if it describes something the compiler does not do. Report either case.

Anything not described here is not part of Gable 0.1. In particular there are no
modules, generics, user-defined types, closures, arrays, exceptions, or
concurrency.

## 1. Source text

* A Gable source file uses the `.gable` extension.
* Source text must be valid UTF-8. A file that is not valid UTF-8 is rejected
  with a diagnostic at the first invalid byte sequence.
* A NUL byte is rejected. Reachable string content cannot contain NUL either
  (see §2.5).
* Identifiers and keywords are ASCII. Non-ASCII characters are allowed only
  inside string literals.
* Line breaks may be LF or CRLF; a lone CR is treated as whitespace.
* There is no byte-order mark handling: a BOM is rejected as a non-ASCII
  character outside a string literal.

## 2. Lexical structure

### 2.1 Whitespace and comments

Whitespace is a space, tab, carriage return, or newline, and separates tokens.

```gable
// line comment: runs to the end of the line
/* block comment: /* nested block comments are supported */ still a comment */
```

An unterminated block comment is an error. Comments do not nest across files,
and there are no doc comments in 0.1.

### 2.2 Keywords

```
fn let var return if else while true false
i8 i16 i32 i64 u8 u16 u32 u64 bool str void
```

Keywords are reserved; they cannot be used as identifiers. `print` is not a
keyword but is reserved as a declaration name because it names the built-in
function.

### 2.3 Identifiers

An identifier starts with an ASCII letter or `_` and continues with ASCII
letters, digits, or `_`. Names are case-sensitive. Consecutive underscores are
allowed; `_` alone is a valid identifier (it is not a discard pattern).

### 2.4 Integer literals

```
42           decimal
0x2A         hexadecimal, requires at least one digit
0b101010     binary, requires at least one digit
```

There is no leading-zero octal notation, no digit separators, and no sign in a
literal: `-5` is unary minus applied to `5`.

A literal must fit in 64 bits; a larger value is an error. A `u` or `U` suffix
is recognised by the lexer and rejected by semantic analysis, because it is
reserved for a future release.

### 2.5 String literals

A string literal is a sequence of bytes written between double quotes:

```
"hello"
"tab:\t"
"quote:\""
"unicode:\u{1F600} or \u00e9"
```

Supported escapes: `\n` `\t` `\r` `\0` `\\` `\"` `\'` `\uXXXX` `\u{X...}`. An
unknown escape is an error. Surrogate code points and code points above
`10FFFF` are errors. A string literal cannot span a line break, and an
unterminated literal is an error.

Because `str` is NUL-terminated at runtime (§4.5), a literal that would produce a
NUL byte (via `\0` or `\u{0}`) is rejected. Backslash escapes are decoded to
UTF-8 bytes at compile time.

## 3. Grammar

The grammar below is EBNF with `{ }` for repetition, `[ ]` for optional parts,
and `|` for alternatives. Terminals are quoted.

```ebnf
program        = { function } end-of-file ;
function       = "fn" ident "(" [ params ] ")" [ "->" type ] block ;
params         = param { "," param } ;
param          = ident ":" type ;
type           = "i8" | "i16" | "i32" | "i64" | "u8" | "u16" | "u32" | "u64"
               | "bool" | "str" | "void" ;

block          = "{" { statement } "}" ;
statement      = let-statement | assign-statement | expr-statement
               | return-statement | if-statement | while-statement | block | ";" ;

let-statement  = ( "let" | "var" ) ident [ ":" type ] "=" expression ";" ;
assign-statement = ident "=" expression ";" ;
expr-statement = expression ";" ;
return-statement = "return" [ expression ] ";" ;
if-statement   = "if" expression block [ "else" ( if-statement | block ) ] ;
while-statement = "while" expression block ;

expression     = unary { binary-op unary } ;
unary          = ( "-" | "!" ) unary | primary ;
primary        = integer-literal | string-literal | "true" | "false"
               | ident | ident "(" [ arguments ] ")" | "(" expression ")" ;
arguments      = expression { "," expression } ;
```

Precedence, from loosest to tightest:

| Level | Operators | Associativity |
| --- | --- | --- |
| 1 | `\|\|` | left |
| 2 | `&&` | left |
| 3 | `==` `!=` | left |
| 4 | `<` `<=` `>` `>=` | left |
| 5 | `+` `-` | left |
| 6 | `*` `/` `%` | left |
| 7 | unary `-` `!` | right |

Notes enforced by the parser:

* Statements are terminated by `;`. Gable does not insert semicolons.
* `if`, `else`, and `while` bodies must be braced blocks.
* An expression statement discards its value. This is how a call to a `void`
  function is written.
* `else if` is accepted as sugar for an `else` whose single statement is another
  `if`; the inner `if` still requires its own braced body.
* Nested function declarations are rejected with a specific diagnostic.
* The `&` and `|` tokens are lexed so that they produce a specific message when
  used: bitwise operators are reserved for a future release.

## 4. Types and values

### 4.1 Types

| Type | Meaning | Range |
| --- | --- | --- |
| `i8` `i16` `i32` `i64` | signed two's-complement integer | −2^(n−1) … 2^(n−1)−1 |
| `u8` `u16` `u32` `u64` | unsigned integer | 0 … 2^n−1 |
| `bool` | boolean | `true`, `false` |
| `str` | immutable UTF-8 text, NUL-terminated | sequences of non-NUL bytes |
| `void` | absence of a value | — |

`void` is only valid as a function return type. It is not a value type: a
variable, parameter, or expression cannot have type `void`, and using a call to a
`void` function where a value is required is an error.

### 4.2 Literals and type inference

Integer literals are untyped until a context determines their type:

* With an expected integer type (parameter of a call, initializer with an
  annotation, the other operand of an arithmetic operator), the literal takes
  that type and is range-checked against it.
* Without an expected type, the literal is `i32` and is range-checked as such.
  A literal that does not fit produces an error that names the type, for example:
  `integer literal 3000000000 does not fit in type 'i32'`.
* `-` applied directly to a literal is folded at compile time, so
  `-9223372036854775808` is a valid `i64` literal and `-2147483648` a valid
  `i32` literal.
* The literal `0` used as a divisor of `/` or `%` is a compile-time error.

There is no implicit conversion between any types. Mixed-width arithmetic such as
`i32 + i64` is an error and requires an explicit decision by the programmer
(which, in 0.1, means not writing that expression).

### 4.3 Arithmetic

All integer arithmetic is **wrapping** two's-complement arithmetic. Overflow is
defined: the result is the mathematically correct value modulo 2^n interpreted in
the result type. `i8` `127 + 1` is `-128`; `u8` `255 + 1` is `0`. This is not
undefined behaviour and does not trap.

Division and remainder truncate toward zero, matching C: `-7 / 2` is `-3` and
`-7 % 2` is `-1`. The sign of a remainder follows the dividend.

Division or remainder by zero:

* is a compile-time error when the divisor is a literal zero;
* otherwise traps at runtime: the program writes
  `gable: runtime error: integer division by zero` to standard error and exits
  with status 70.

The single overflowing division, `INT_MIN / -1`, wraps to `INT_MIN` and its
remainder is `0`, matching wrapping arithmetic.

Unary `-` is only defined for signed integers; applying it to an unsigned value
is a compile-time error.

### 4.4 Booleans

`bool` values are `true` and `false`. There is no implicit conversion between
`bool` and integers: `if 1 { }` is an error. `&&` and `||` short-circuit and
their operands must be `bool`. `==` and `!=` are defined for integers and for
`bool`.

### 4.5 Strings

`str` is an immutable sequence of UTF-8 bytes with a terminating NUL. Gable 0.1
provides no string operations other than printing: no concatenation,
comparison, indexing, or length. Applying `==`, `<`, or arithmetic to `str`
operands is a compile-time error.

## 5. Declarations and scopes

### 5.1 Functions

```
fn add(a: i64, b: i64) -> i64 {
    return a + b;
}
```

* A function without `-> type` returns `void`.
* Parameters are immutable bindings; assigning to a parameter is an error.
* Parameter names must be unique within the function.
* Function names share one namespace per program. All functions are visible
  throughout the program, so recursion and calls to functions declared later
  work. Duplicate function names are an error.
* Gable 0.1 has no overloading, default arguments, variadic parameters, or
  nested functions.
* Function names cannot be used as values; only a direct call `name(...)` is
  allowed.
* A non-`void` function must return a value on every path that can reach the end
  of its body. The check is structural: a `return` statement, an `if`/`else`
  where both branches always return, or a block ending in such a statement,
  counts as returning. Loops are never assumed to terminate.

### 5.2 The entry point

A compiled program must declare exactly:

```
fn main() -> i32
```

`main` takes no parameters and returns `i32`. The value becomes the process exit
status (truncated to the platform's range by the operating system). Any other
signature is a compile-time error.

### 5.3 Variables

```
let answer: i32 = 42;     // immutable binding
var counter: i32 = 0;     // mutable binding
counter = counter + 1;
```

* Every binding requires an initializer; there is no uninitialized declaration.
* An annotation is optional. Without one, the initializer's type is used, which
  means an unannotated integer literal is `i32`.
* Assigning to a `let` binding is an error. `var` bindings can be reassigned,
  but only with a value of the same type.
* A binding becomes visible after its initializer, so `let x: i32 = x;` refers to
  an outer `x` or reports an undefined name.
* Redeclaring a name in the same scope is an error. Shadowing an outer binding in
  a nested block is allowed.
* Blocks introduce scopes; a binding is not visible after its block ends.
* Variables and functions live in separate namespaces: a variable may share a
  name with a function, and a call `name(...)` always resolves to the function.

### 5.4 Conditionals and loops

```
if a > b {
    print("a");
} else if a == b {
    print("same");
} else {
    print("b");
}

while remaining > 0 {
    remaining = remaining - 1;
}
```

Conditions must have type `bool`. There is no `break`, `continue`, `for`, or
`loop` in 0.1.

## 6. Built-in functions

The only built-in is `print`, which writes one value followed by `\n` to
standard output:

| Argument type | Output |
| --- | --- |
| `str` | the bytes of the string |
| `bool` | `true` or `false` |
| signed integer | decimal digits, `-` for negative values |
| unsigned integer | decimal digits |

`print` returns `void`, takes exactly one argument, and cannot be shadowed or
redeclared. It is not part of the runtime ABI; the compiler lowers it to runtime
calls (§7).

## 7. Runtime interface

Programs link against the Gable runtime, a C17 library with a C ABI:

```c
void     gable_rt_print_str(const char *value);
void     gable_rt_print_bool(bool value);
void     gable_rt_print_i64(int64_t value);
void     gable_rt_print_u64(uint64_t value);
int64_t  gable_rt_div_i64(int64_t lhs, int64_t rhs);
int64_t  gable_rt_rem_i64(int64_t lhs, int64_t rhs);
uint64_t gable_rt_div_u64(uint64_t lhs, uint64_t rhs);
uint64_t gable_rt_rem_u64(uint64_t lhs, uint64_t rhs);
_Noreturn void gable_rt_trap(const char *message);
```

Narrow integer types are widened to `i64`/`u64` before calling division helpers;
the quotient is truncated back to the narrow type, which preserves the wrapping
semantics described in §4.3.

## 8. Diagnostics

Errors are reported on standard error as:

```
path:line:column: severity: message
  <line> | <source line>
  <pad>  | <caret>
```

`line` and `column` are one-based; columns count Unicode scalar values. The
compiler reports every error it can in one run. Semantic analysis is skipped when
syntax errors are present, so follow-on errors are not reported.

Exit statuses are documented in [getting-started.md](getting-started.md).

## 9. Known limitations

* One source file per program; no imports or modules.
* No arrays, records, enums, pointers, references, generics, or closures.
* No standard library beyond `print`.
* No string operations.
* No bitwise or shift operators; `&` and `|` are reserved.
* No `break`/`continue`, no `for`.
* No explicit casts. Programs must be written so that types already match.
* No compile-time constants, no `const` evaluation beyond literal folding.
* No debug information emitted for Gable source; debugging happens at the C
  level in generated code.
* Operand evaluation order is unspecified except that `&&` and `||` short-circuit
  and their left operand is evaluated first. Programs must not depend on the
  order in which other operands are evaluated when they have side effects.

## 10. Evolution policy

This document is versioned with the compiler. Any change to the grammar,
semantics, or runtime interface must update this file and its tests in the same
change. Language changes are proposed as GitHub issues before implementation; see
[CONTRIBUTING.md](../CONTRIBUTING.md).

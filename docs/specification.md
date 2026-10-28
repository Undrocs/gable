# Gable language specification — version 0.2

Status: **experimental**. This document describes the language implemented by the
`gable` 0.2 compiler. Where the implementation and this document disagree, the
implementation is wrong if the behaviour is unspecified here, and this document
is wrong if it describes something the compiler does not do. Report either case.

Anything not described here is not part of Gable 0.2. In particular there are no
generics, traits, closures, pointers, exceptions, concurrency, dynamic arrays, or
a package manager.

## 1. Source text

* A Gable source file uses the `.gable` extension.
* Source text must be valid UTF-8; a file that is not is rejected at the first
  invalid byte sequence.
* A NUL byte is rejected, and string content cannot contain NUL (§2.5).
* Identifiers and keywords are ASCII. Non-ASCII characters are allowed only inside
  string literals.
* Line breaks may be LF or CRLF; a lone CR is whitespace.
* A byte-order mark is rejected as a non-ASCII character outside a string literal.

## 2. Lexical structure

### 2.1 Whitespace and comments

```gable
// line comment: runs to the end of the line
/* block comment: /* nested block comments are supported */ still a comment */
```

An unterminated block comment is an error. There are no doc comments.

### 2.2 Keywords

```
fn let var return if else while true false struct import as break continue for in
i8 i16 i32 i64 u8 u16 u32 u64 bool str void
```

Keywords cannot be used as identifiers. `print`, `eprint`, and the names of the
built-in functions in §9 are reserved and cannot be declared.

### 2.3 Identifiers

An identifier starts with an ASCII letter or `_` and continues with ASCII letters,
digits, or `_`. Names are case-sensitive. `_` alone is a valid identifier; there
is no discard pattern.

### 2.4 Integer literals

```
42          decimal
0x2A        hexadecimal, at least one digit required
0b1010      binary, at least one digit required
```

A literal must fit in 64 bits; larger values are a lexer error. A literal has no
type of its own: it takes the type required by its context (§5.2). A `u` suffix is
recognized but rejected with a diagnostic, because unsigned types are written
explicitly instead.

### 2.5 String literals

A string literal is enclosed in double quotes and produces a `str` (§6.4).

```
"plain"
"tab:\t newline:\n quote:\" backslash:\\ carriage:\r"
"\u{41}"     a Unicode code point in braces
"\u00e9"     a Unicode code point with exactly four hex digits
```

Escape sequences are `\n`, `\t`, `\r`, `\\`, `\"`, `\u{...}`, and `\uXXXX`. Any
other escape is an error, as is a surrogate code point, a NUL character, an
unterminated literal, or a raw line break inside a literal.

### 2.6 Operators and punctuation

```
=  ==  !=  <  <=  >  >=  +  -  *  /  %  &&  ||  !  as
(  )  {  }  [  ]  ,  :  ;  .  ..  ->
```

`&` and `|` are reserved for future use and rejected by the parser.

## 3. Program structure

A program is a root source file together with every file it imports, directly or
transitively. Imports must appear before any other declaration in a file:

```gable
import "storage.gable";
import "text.gable";
```

* The path is resolved relative to the importing file. Absolute paths are
  accepted but discouraged.
* Every file is loaded and parsed once. Importing an already loaded file has no
  effect, so diamond-shaped graphs are fine.
* Import cycles are an error that names the path closing the cycle.
* All declarations of all files form one program-wide namespace. Function and
  struct declarations may refer to each other in any order and across files.
* Two declarations with the same name, whether in one file or in different files,
  are an error. There is no per-file privacy and no package manager.

## 4. Declarations

### 4.1 Functions

```gable
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn log_message(text: str) {
    print(text);
}
```

A function name is program-wide. Parameters are separated by commas, typed, and
immutable inside the body (§7.3). The return type follows `->`; omitting it means
`void`. `-> void` is accepted and means the same thing.

Functions may call themselves and each other regardless of declaration order.
There is no overloading: one function per name.

Every non-`void` function must return a value on every path. The check is
conservative: a `return` inside an `if` counts only when the `if` has an `else`
and both branches always return, and loops never count as returning.

### 4.2 The entry point

An executable requires exactly one function `main` with no parameters and the
return type `i32`:

```gable
fn main() -> i32 {
    return 0;
}
```

The returned value becomes the process exit status. Command-line arguments are
read with `arg_count()` and `arg(i)` (§9).

### 4.3 Structs

```gable
struct Point {
    x: i32,
    y: i32,
}
```

A struct is a named product of fields in declaration order. A trailing comma
after the last field is allowed. Field names must be unique within the struct and
must not repeat a field of another struct (fields live in the struct's own
namespace). A field may have any type except `void`, may be another struct, or an
array of structs.

A struct may not contain itself by value, directly or through other structs and
arrays, because such a value would have no finite size. A struct can be used as a
type from any file in the program.

## 5. Types

### 5.1 Built-in types

| Type | Meaning |
| --- | --- |
| `i8` `i16` `i32` `i64` | signed two's-complement integers |
| `u8` `u16` `u32` `u64` | unsigned integers |
| `bool` | `true` or `false` |
| `str` | UTF-8 byte sequence with an explicit length |
| `void` | absence of a value; only as the result type of a function |

Struct names introduce struct types. `[T; N]` is an array type (§6.5) with
`N` from 1 to 1,000,000.

### 5.2 Literals and conversions

There are no implicit conversions between types. An integer literal is the one
exception: it takes the type required by its context and is range checked there.

```gable
var a: i64 = 3000000000;      // the literal is an i64
let b: i32 = 3000000000;      // error: does not fit in i32
var c: i32 = 1;
let d: i64 = c;               // error: no implicit i32 -> i64 conversion
let e: i64 = c as i64;        // explicit conversion
```

Without a context an integer literal is `i32`. Conversions use `as` and are
defined in §8.5.

## 6. Values

### 6.1 Copy semantics

Every Gable value is copied when it is assigned, passed as an argument, returned,
or read out of an aggregate. There are no references and no aliasing, so a
function cannot modify its caller's variables except by returning a new value.

### 6.2 Integers

Integers are fixed-width. Arithmetic wraps modulo 2<sup>width</sup> and never
overflows into undefined behaviour:

```gable
var x: i8 = 127;
x = x + 1;      // -128
```

### 6.3 Booleans

`bool` supports `==` and `!=` only. Conditions of `if` and `while` must have type
`bool`; integers are not accepted as conditions.

### 6.4 Strings

A `str` is an immutable sequence of bytes with an explicit length. It is not
NUL-terminated, may contain any byte except NUL, and may be arbitrarily long.

* Length is counted in bytes: `text.len` has type `i64`.
* Literals are valid UTF-8. Operations such as slicing work on bytes and may
  split a multi-byte character; that is documented behaviour, not corruption.
* `==` and `!=` compare contents, not identity.
* `+` concatenates and allocates a new string. The result is valid until the
  process exits: Gable 0.2 has no memory reclamation (§12).
* Strings cannot be indexed. Use `str_slice`, `str_find`, and `str_trim` (§9).
* Other operators on strings are compile-time errors.

### 6.5 Arrays

```gable
let values: [i32; 3] = [1, 2, 3];
print(values[1]);
values[1] = 5;              // requires `var`
```

An array has a compile-time length and homogeneous element type. An array
literal is only valid with an expected array type; its length must match the
declared length exactly.

Element access is bounds checked at run time. An out-of-range index terminates
the program with a diagnostic and exit status 70 (§11); it never reads or writes
out of bounds. Indices may be signed or unsigned; a negative signed index is
rejected at run time and unsigned indices are compared against the length.

`values.len` is the element count and has type `i64`. Arrays are copied like any
other value.

### 6.6 Structs

```gable
let point: Point = Point(3, 4);
print(point.x);
point.x = 5;                // requires `var`
```

Construction lists every field in declaration order; there are no keyword
arguments, defaults, or partial construction. Field access and assignment use
`.`.

Structs are compared and printed field by field, not with operators or `print`:
`==` on structs is a compile-time error and `print(point.x)` is required.

## 7. Statements

Every statement ends with `;` except blocks, `if`, `while`, and `for`.

### 7.1 Bindings

```gable
let answer: i32 = 42;
var counter: i32 = 0;
let inferred = answer;
```

`let` declares an immutable binding, `var` a mutable one. Every binding has an
initializer; there are no uninitialized variables. The type annotation is
optional and is inferred from the initializer when omitted. A binding is visible
from the statement after its declaration to the end of the enclosing block; an
inner block may shadow an outer binding.

### 7.2 Assignment

```gable
counter = counter + 1;
point.x = 7;
values[0] = 9;
```

Only `var` bindings, their fields, and their array elements are assignable.
Assigning to a `let` binding, a parameter, or a loop variable is an error. The
right-hand side must have exactly the target's type.

When both sides have effects, the target's index expressions are evaluated
before the right-hand side (§8.6).

### 7.3 Parameters and loop variables

Parameters are immutable; copy a parameter into a `var` binding to modify it. The
loop variable of a `for` statement is an immutable copy of the current element or
index.

### 7.4 `if`, `while`, blocks

```gable
if condition {
    ...
} else if other {
    ...
} else {
    ...
}

while condition {
    ...
}
```

Bodies must be braced. `else if` is an `else` whose body is a single `if`. A
block introduces a scope.

### 7.5 `for`

```gable
for i in 0..10 {           // 0, 1, ..., 9; the upper bound is excluded
    ...
}

for value in values {      // one iteration per element
    ...
}
```

A range loop requires integer bounds of the same type; a literal bound takes the
other bound's type. Both bounds are evaluated exactly once, before the first
iteration, in start-then-limit order. Iteration is ascending; when the start is
greater than or equal to the limit the body does not run. The loop counter cannot
be modified.

An array loop copies the array expression once and iterates over that copy, so
modifying the original binding inside the body does not affect the iteration. The
loop variable is a copy of the element.

### 7.6 `break` and `continue`

`break` exits the innermost enclosing loop, `continue` starts its next iteration.
Using either outside a loop is a compile-time error. In a `for` loop, `continue`
still performs the increment.

### 7.7 `return`

`return` without a value is only valid in a `void` function; `return value` is
required in every other function and must have exactly the declared type.

## 8. Expressions

### 8.1 Operators and precedence

From tightest to loosest:

| Level | Operators |
| --- | --- |
| postfix | call `f(...)`, field `.name`, index `[i]`, cast `as T` |
| unary | `-` `!` |
| multiplicative | `*` `/` `%` |
| additive | `+` `-` |
| comparison | `<` `<=` `>` `>=` |
| equality | `==` `!=` |
| logical AND | `&&` |
| logical OR | `\|\|` |

`as` binds tighter than binary operators, so `a + b as i64` is `a + (b as i64)`.
Parentheses group as usual.

### 8.2 Arithmetic

`+ - * / %` require both operands to have the same integer type (a literal adapts
to the other operand). The result has that type. `+` is also string
concatenation.

Division truncates toward zero and the remainder takes the sign of the dividend:

```
 7 / 2 ==  3      7 % 2 ==  1
-7 / 2 == -3     -7 % 2 == -1
```

Division or remainder by zero is a runtime trap (§11). A constant divisor of zero
is a compile-time error. The only overflowing division, the minimum value divided
by -1, wraps to the minimum value, and the corresponding remainder is zero.

### 8.3 Comparison

`< <= > >=` require two integers of the same type and produce `bool`.
`== !=` accept two integers of the same type, two `bool`s, or two `str`s, and
produce `bool`. `==` on structs or arrays is a compile-time error.

### 8.4 Boolean operators

`&&` and `||` require `bool` operands and short-circuit: the right operand is
evaluated only when the result is not already determined. `!` negates a `bool`.
Conditions do not accept integers.

### 8.5 Conversions

`expr as T` converts between integer types and is defined as the two's-complement
bit pattern truncated or extended to the target width:

* Widening a signed value sign-extends; widening an unsigned value zero-extends.
* Narrowing keeps the low bits: `300 as u8 == 44`, `-1 as u8 == 255`.
* Any other conversion is a compile-time error; use `str_of_i64` or `parse_i64`
  to move between text and numbers.

Conversions never trap and never produce an unspecified value.

### 8.6 Evaluation order

Gable defines evaluation order explicitly. Operands of binary operators are
evaluated left to right, call arguments are evaluated left to right, index
expressions are evaluated before the element is accessed, and the receiver of a
field access or index is evaluated before the subscript. `&&` and `||` evaluate
their right operand only when required, and a short-circuited operand has no
effect at all.

```gable
print(combine(next("first"), next("second")));   // prints first, then second
```

This order is guaranteed, not inherited from the C backend.

## 9. Built-in functions

Built-ins are ordinary calls; they cannot be used as values and their names are
reserved.

| Function | Signature | Meaning |
| --- | --- | --- |
| `print(value)` | `str` / `bool` / any integer | writes the value and a newline to standard output |
| `eprint(value)` | same | writes to standard error |
| `arg_count()` | `-> i32` | number of command-line arguments, including the program name |
| `arg(index)` | `i32 -> str` | argument at `index`; empty string when out of range |
| `fs_read_text(path)` | `str -> FileReadResult` | reads a whole file |
| `fs_write_text(path, text)` | `str, str -> FileWriteResult` | replaces a whole file |
| `fs_write_text_atomic(path, text)` | `str, str -> FileWriteResult` | replaces a whole file, or leaves the old one |
| `fs_lock(path)` | `str -> FileWriteResult` | takes the advisory lock for `path` |
| `fs_unlock(path)` | `str -> FileWriteResult` | releases the advisory lock for `path` |
| `str_of_i64(value)` | `i64 -> str` | decimal representation |
| `str_of_bool(value)` | `bool -> str` | `"true"` or `"false"` |
| `parse_i64(text)` | `str -> ParseResult` | optional sign, surrounding ASCII whitespace ignored, overflow rejected |
| `str_find(haystack, needle)` | `str, str -> FindResult` | byte offset of the first occurrence |
| `str_slice(text, start, length)` | `str, i32, i32 -> SliceResult` | copies a byte range |
| `str_trim(text)` | `str -> str` | removes leading and trailing ASCII whitespace (space, tab, newline, carriage return, vertical tab, form feed; not other Unicode spaces) |

The result types are ordinary structs declared by the prelude, so their fields
are read like any other struct:

```gable
struct FileReadResult  { ok: bool, text: str, error: str, code: i32 }
struct FileWriteResult { ok: bool, error: str, code: i32 }
struct ParseResult     { ok: bool, value: i64 }
struct FindResult      { ok: bool, index: i64 }
struct SliceResult     { ok: bool, text: str }
```

Failing operations return `ok == false` with a message in `error`; they do not
trap. `error` comes from the operating system and is localised, so a program must
branch on `code`:

| `code` | Meaning |
| --- | --- |
| 0 | success |
| 1 | no such file or directory |
| 2 | permission denied |
| 3 | invalid path: empty, longer than 4096 bytes, not valid UTF-8 on Windows, or containing a NUL byte |
| 4 | file is larger than the 64 MiB limit of `fs_read_text` |
| 5 | other input or output failure |
| 6 | the file is locked by another program (`fs_lock`) |

A path that contains a NUL byte is rejected as a whole rather than truncated at
the NUL: `"dir/file\0.orig"` and `"dir/file"` name the same file to the operating
system, so accepting the first would let a program written to preserve the second
overwrite it. Gable strings may still contain NUL bytes; only path arguments are
restricted this way.

`fs_read_text` reads at most 64 MiB and rejects larger files.

`fs_write_text` truncates the file and writes the new contents in place, so a
program that stops halfway through leaves a partial file. `fs_write_text_atomic`
writes a temporary file in the same directory and renames it over the target:
readers see either the old contents or the new ones, and a failed or interrupted
write leaves the previous contents untouched. It is slower, because every write
creates and renames a file, and it refuses to replace a file the process may not
write. Neither function forces the data to disk, so a machine that loses power
immediately afterwards can still lose the write.

`fs_lock` and `fs_unlock` provide the advisory lock that a read-modify-write
sequence needs, because two programs that read the same file and then write it
back would otherwise lose one of the updates. `fs_lock` creates `<path>.lock`
exclusively; while another program holds it, it retries for about a second and
then reports code 6. A lock file older than ten seconds is treated as abandoned by
a program that died without unlocking, and removed. `fs_unlock` removes the lock
file and succeeds when there is none. The lock is advisory: a program that never
calls `fs_lock` is not stopped from writing, and the data file itself is never
locked. Locks are per file name, not per file: two names for the same file, such
as a relative and an absolute path, produce two different lock files.

On Windows, command-line arguments are converted from the process ANSI code page
to UTF-8, and file paths are converted from UTF-8 to UTF-16 for the operating
system, so accented and non-BMP characters work. On POSIX hosts the bytes are
passed through unchanged.

## 10. Running a program

```
gable build <file.gable> [-o <path>]
gable run <file.gable> [-- program arguments...]
gable check <file.gable>
```

`check` performs the same front-end work as `build` and writes nothing.
`run` builds a temporary executable and forwards its exit status; arguments after
`--` are passed to the program.

On Windows, an output path without a file extension gets `.exe` appended; on
POSIX hosts the path is used as given.

## 11. Runtime behaviour

A Gable program can terminate in three ways:

1. It returns from `main`; the returned value is the exit status.
2. A runtime trap occurs. A trap writes `gable: runtime error: <message>` to
   standard error and exits with status 70. Traps are: integer division by zero,
   remainder by zero, and an out-of-bounds array index.
3. The process is terminated by the operating system.

Standard output is flushed before a trap exits, so output produced earlier is not
lost.

## 12. Memory

Gable 0.2 has no memory reclamation: strings produced by concatenation,
conversion, slicing, and trimming are never freed, and `fs_read_text` buffers are
kept for the life of the process. This is intentional for this version and is the
main reason the language is not ready for long-running programs. Values of
fixed-size types live on the stack.

## 13. Compiler diagnostics

Diagnostics are written to standard error in the form

```
path:line:column: severity: message
  line | source text
       | ^~~~~~
```

Every diagnostic names the file it belongs to, so a multi-file error is
unambiguous. Notes follow the error they explain. The compiler reports as many
independent errors as it can in one run and does not stop at the first.

Compiler exit statuses: `0` success, `1` compilation failed, `2` usage error or
unusable output path, `70` internal failure (missing C toolchain, or generated C
that does not compile). `run` forwards the program's exit status, so a program
that exits with 1, 2, or 70 is indistinguishable from the corresponding compiler
failure.

# Gable language specification — version 0.3

Status: **experimental**. This document describes the language implemented by the
`gable` 0.3 compiler. Where the implementation and this document disagree, the
implementation is wrong if the behaviour is unspecified here, and this document
is wrong if it describes something the compiler does not do. Report either case.

Anything not described here is not part of Gable 0.3. In particular there are no
generics, traits, closures, pointers, exceptions, concurrency, or a package
manager.

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
int i8 i16 i32 i64 u8 u16 u32 u64 bool str void
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
type of its own: it takes the type required by its context (§5.2), and is `int`
when nothing else says otherwise. A `u` suffix is recognized but rejected with a
diagnostic, because unsigned types are written explicitly instead.

### 2.5 String literals

A string literal is enclosed in double quotes and produces a `str` (§6.4).

```
"plain"
"tab:\t newline:\n quote:\" backslash:\\ carriage:\r"
"\u{41}"     a Unicode code point in braces
"\u00e9"     a Unicode code point with exactly four hex digits
```

Escape sequences are `\n`, `\t`, `\r`, `\\`, `\"`, `\$`, `\u{...}`, and `\uXXXX`.
Any other escape is an error, as is a surrogate code point, a NUL character, an
unterminated literal, or a raw line break inside a literal.

A string literal may contain values written in place:

```gable
let name = "ada"
let count = 3
print("hello $name")          // hello ada
print("$count of ${count + 4}")   // 3 of 7
print("cost: \$5")            // cost: $5
```

* `$name` inserts the value of a variable, parameter, or loop variable. The name
  ends at the first character that cannot continue an identifier.
* `${ expression }` inserts any expression, so a field, a call, or arithmetic can
  be written inline: `"total: ${items.len}"`.
* `\$` is a literal dollar sign. A `$` that is followed by neither a name nor `{`
  is an ordinary character, so `"$5"` and `"a$"` need no escape.
* The expression is converted as `print` converts it: `str`, `bool`, and any
  integer type are accepted. A struct, array, or list is a compile-time error, and
  the diagnostic suggests converting first.
* Interpolation nests (a `${...}` containing a string literal that interpolates)
  to 8 levels; beyond that it is an error rather than unbounded work.

Interpolation is a spelling of concatenation, not a new kind of value: the result
is a `str` built by joining the parts, and it allocates like `+` does.

### 2.6 Operators and punctuation

```
=  ==  !=  <  <=  >  >=  +  -  *  /  %  &&  ||  !  as
(  )  {  }  [  ]  ,  :  ;  .  ..  ->  $
```

`&` and `|` are reserved for future use and rejected by the parser. `;` is
accepted wherever a line break would end a statement (§7).

## 3. Program structure

A program is a root source file together with every file it imports, directly or
transitively. Imports must appear before any other declaration in a file:

```gable
import "storage.gable"
import "text.gable"
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
* A compilation is limited to 512 source files, 64 MiB per file and 256 MiB in
  total, and imports may be nested at most 64 levels deep. Exceeding any of these
  is reported as an error rather than exhausting memory or the stack.

## 4. Declarations

### 4.1 Functions

```gable
fn add(a: int, b: int) -> int {
    return a + b
}

fn log_message(text: str) {
    print(text)
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

An executable requires exactly one function `main` with no parameters. It returns
`void` (the process then exits with status 0) or `int` (the returned value is the
exit status):

```gable
fn main() {
    print("hello")
}
```

```gable
fn main() -> int {
    return 0
}
```

Command-line arguments are read with `arg_count()` and `arg(i)` (§9).

### 4.3 Structs

```gable
struct Point {
    x: int
    y: int
}
```

A struct is a named product of fields in declaration order. Fields are separated
by line breaks, and a comma after a field is accepted and ignored. Field names
must be unique within the struct and must not repeat a field of another struct
(fields live in the struct's own namespace). A field may have any type except
`void`, may be another struct, or a list of structs.

A struct may not contain itself by value, directly or through other structs and
arrays, because such a value would have no finite size. A struct can be used as a
type from any file in the program.

## 5. Types

### 5.1 Built-in types

| Type | Meaning |
| --- | --- |
| `int` | signed 64-bit integer, the everyday numeric type |
| `i8` `i16` `i32` `i64` | signed two's-complement integers of that width; `i64` is the same type as `int` |
| `u8` `u16` `u32` `u64` | unsigned integers |
| `bool` | `true` or `false` |
| `str` | UTF-8 byte sequence with an explicit length |
| `void` | absence of a value; only as the result type of a function |

Counts, lengths, and indices are `int` throughout the language and the built-in
functions, so `.len`, `arg(i)`, and a slice bound can be used together without
conversions. The narrower widths exist for data that comes from a file or a
protocol and must be kept at its own width; a program that has no such reason
uses `int`.

Struct names introduce struct types. `[T; N]` is an array type (§6.5) with
`N` from 1 to 1,000,000, and `[T]` a growable list (§6.6).

### 5.2 Literals and conversions

There are no implicit conversions between types. An integer literal is the one
exception: it takes the type required by its context and is range checked there.

```gable
var a: i64 = 3000000000      // the literal is an i64
let b: int = 3000000000      // ... and the same type under its everyday name
let c: i32 = 3000000000      // error: does not fit in i32
var d: i32 = 1
let e: int = d               // error: no implicit i32 -> int conversion
let f: int = d as int        // explicit conversion
```

Without a context an integer literal is `int`. Conversions use `as` and are
defined in §8.5.

## 6. Values

### 6.1 Copy semantics

Every Gable value is copied when it is assigned, passed as an argument, returned,
or read out of an aggregate. There are no references and no aliasing, so a
function cannot modify its caller's variables except by returning a new value.

There is one deliberate exception, and it is about what a value *is* rather than
how copying works: a list (§6.6) is a handle to shared storage. Copying the handle
is still a copy of the value, and the variable it lives in is still independent —
but both handles name the same elements, so appending through one is visible
through the other. Strings and arrays are values all the way down and are copied
in full.

### 6.2 Integers

Integers are fixed-width. Arithmetic wraps modulo 2<sup>width</sup> and never
overflows into undefined behaviour:

```gable
var x: i8 = 127
x = x + 1        // -128
```

### 6.3 Booleans

`bool` supports `==` and `!=` only. Conditions of `if` and `while` must have type
`bool`; integers are not accepted as conditions.

### 6.4 Strings

A `str` is an immutable sequence of bytes with an explicit length. It is not
NUL-terminated, may contain any byte except NUL, and may be arbitrarily long.

* Length is counted in bytes: `text.len` has type `int`.
* Literals are valid UTF-8. Operations such as slicing work on bytes and may
  split a multi-byte character; that is documented behaviour, not corruption.
* `==` and `!=` compare contents, not identity.
* `+` concatenates and allocates a new string, and a string literal with `$name`
  or `${...}` in it is the same concatenation written in place (§2.5). The result
  stays valid for as long as the program can reach it; strings that are no longer
  reachable are reclaimed automatically (§12).
* Strings cannot be indexed. Use `.slice`, `.find`, and `.trim` (§8.7).
* Other operators on strings are compile-time errors.

### 6.5 Arrays

```gable
var values: [int; 3] = [1, 2, 3]
print(values[1])
values[1] = 5              // `let` would make every element read-only
```

An array has a compile-time length and homogeneous element type. An array literal
is valid where an array type is expected, and its length must match the declared
length exactly. The type is always written out, because the length is part of it;
a literal with no expected type is a list (§6.6) instead.

Element access is bounds checked at run time. An out-of-range index terminates
the program with a diagnostic and exit status 70 (§11); it never reads or writes
out of bounds. Indices may be signed or unsigned; a negative signed index is
rejected at run time and unsigned indices are compared against the length.

`values.len` is the element count and has type `int`. Arrays are copied like any
other value.

### 6.6 Lists

An array has a fixed length, which is the wrong tool for data that grows. A list
has no length in its type and can be appended to:

```gable
let names: [str] = []          // an empty list of strings
names.push("ada")
names.push("grace")
print(names.len)               // 2
print(names[0])                // ada
names[1] = "hopper"            // replaces an element
for name in names {
    print(name)
}
```

* `[T]` is a growable list of `T`; `[T; N]` is a fixed-length array. The two are
  different types and do not convert into each other.
* A list literal is valid where a list type is expected: `let counts: [int] =
  [1, 2]` starts a list with two elements, and `let empty: [int] = []` starts an
  empty one. Where no type is expected, the elements must all have the same type
  and the literal is a list of them, because a list is what a program reaches for
  by default:
  ```gable
  let primes = [2, 3, 5]     // a [int] holding 2, 3, 5
  primes.push(7)
  let words = ["ada", "grace"]
  print(words.join(" and "))  // ada and grace
  ```
  An empty literal with no expected type is a compile-time error, because there
  is nothing to say what it would hold.
* `list.push(value)` appends `value` and returns nothing. The value type must be
  the element type of the list. The free function `list_push(list, value)` is the
  same operation written the other way round and remains available.
* `list[i]` reads an element and `list[i] = value` replaces one. Both are bounds
  checked, and an out-of-range index traps with exit status 70 (§11).
* `list.len` is the element count and has type `int`.
* `for item in list { ... }` visits the elements, in order, that were in the list
  when the loop started.
* Elements may be any type that is not `void`: scalars, strings, structs, arrays,
  and other lists (`[[int]]`).

A list value is a handle to shared storage, not the elements themselves. Copying a
list, passing it to a function, or returning it copies the handle: every copy sees
the same elements and the same length. This is what makes `items.push(x)` inside a
function affect the caller's list, and it is the one place in Gable where a `let`
binding does not freeze what it refers to:

```gable
fn add(values: [int]) {
    values.push(1)             // visible to the caller
}

fn main() {
    let numbers: [int] = []
    add(numbers)
    print(numbers.len)         // 1
}
```

Mutation is always visible in the source: an element is replaced by `list[i] = v`
and a list grows only where `.push` is written. Nothing is copied implicitly when
a list is passed, so the cost of a call does not depend on the list's length. The
memory behind a list is reclaimed like any other heap value when no list refers to
it (§12).

### 6.7 Structs

```gable
var point = Point(3, 4)
print(point.x)
point.x = 5
```

Construction lists every field in declaration order; there are no keyword
arguments, defaults, or partial construction. Field access and assignment use `.`.

Structs are compared and printed field by field, not with operators or `print`:
`==` on structs is a compile-time error and `print(point.x)` is required.

## 7. Statements

A statement ends at a line break, at `;`, at `}`, or at the end of the file.
Blank lines and comments do not end anything, and `;` is accepted and ignored, so
both of these are the same three assignments:

```gable
var total = 0
total = total + 1
total = total * 2
```

```gable
var total = 0;
total = total + 1;
total = total * 2;
```

An expression may continue on the next line: a line break only ends a statement
when the statement could end there, so `let n = 1 +` followed by `2` on the next
line is one statement. Writing a second statement on the same line therefore needs
`;`, and the diagnostic for forgetting it says so:

```
main.gable:2:15: error: expected a line break after a declaration, found 'let'
       | note: a line break ends a statement; ';' is also accepted
```

### 7.1 Bindings

```gable
let answer = 42
var counter = 0
let doubled: int = answer * 2
```

`let` declares an immutable binding, `var` a mutable one. Every binding has an
initializer; there are no uninitialized variables. The type annotation is optional
and is inferred from the initializer when omitted. A binding is visible from the
statement after its declaration to the end of the enclosing block; an inner block
may shadow an outer binding.

A binding of a value that can report failure can handle the failure on the spot:

```gable
let text = fs_read_text(path) else {
    eprint("cannot read $path: ${text.error}")
    return 1
}
print(text.text)
```

* The initializer must have a type with a `bool` field named `ok`; the result
  structs of §9 qualify, and so does a struct of the program's own.
* The `else` block runs when `ok` is false. The failed value is in scope inside it
  under the binding's name, so `text.error` is the message that explains why.
* The block must end the statement — every path through it must `return`, `break`,
  or `continue` — because the binding only exists when the operation succeeded.
  A block that can fall through is a compile-time error.
* The binding is immutable; an `ok` value that came from `var` is still bound as a
  `let`.

### 7.2 Assignment

```gable
counter = counter + 1
point.x = 7
values[0] = 9
```

Only `var` bindings, their fields, and their array or list elements are
assignable. Assigning to a `let` binding, a parameter, or a loop variable is an
error. The right-hand side must have exactly the target's type.

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

`=` in a condition is an error that names the mistake and suggests `==`, and the
diagnostic is produced once for the statement rather than cascading into the
branches.

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

A list loop evaluates the list once and reads its length when the loop starts. It
visits that many elements in order; elements are read one at a time, so a body
that appends to the list does not disturb the iteration but also does not extend
it. The loop variable is a copy of the element.

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
| postfix | call `f(...)`, method call `.name(...)`, field `.name`, index `[i]`, cast `as T` |
| unary | `-` `!` |
| multiplicative | `*` `/` `%` |
| additive | `+` `-` |
| comparison | `<` `<=` `>` `>=` |
| equality | `==` `!=` |
| logical AND | `&&` |
| logical OR | `\|\|` |

`as` binds tighter than binary operators, so `a + b as int` is `a + (b as int)`.
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

Conversions never trap and never produce an unspecified value. A mismatch between
two integer types is reported with a note that says Gable does not convert on its
own, which is the mistake the rule most often causes.

### 8.6 Evaluation order

Gable defines evaluation order explicitly. Operands of binary operators are
evaluated left to right, call arguments are evaluated left to right, index
expressions are evaluated before the element is accessed, and the receiver of a
method call, field access, or index is evaluated before its arguments or
subscript. `&&` and `||` evaluate their right operand only when required, and a
short-circuited operand has no effect at all.

```gable
print(combine(next("first"), next("second")))    // prints first, then second
```

This order is guaranteed, not inherited from the C backend.

### 8.7 Methods

A value can answer questions about itself; `value.name(arguments)` calls the
built-in that takes the value as its first argument:

| Call | Same as | Meaning |
| --- | --- | --- |
| `text.trim()` | `str_trim(text)` | leading and trailing whitespace removed |
| `text.split(separator)` | `str_split(text, separator)` | a `[str]` of the pieces |
| `text.find(needle)` | `str_find(text, needle)` | a `FindResult` |
| `text.slice(start, length)` | `str_slice(text, start, length)` | a `SliceResult` |
| `parts.join(separator)` | `str_join(parts, separator)` | one `str` from a `[str]` |
| `list.push(value)` | `list_push(list, value)` | appends to a list |

Methods exist only on `str` and lists. `.len` is a field-like property, not a
method, so `text.len` is written without parentheses; `text.len()` is an error
that says so. A struct's fields are read the same way. Calls are checked exactly
like the free function they stand for: same argument count, same argument types,
same result.

## 9. Built-in functions

Built-ins are ordinary calls; they cannot be used as values and their names are
reserved. Most of them have a method spelling on the value they operate on
(§8.7).

| Function | Signature | Meaning |
| --- | --- | --- |
| `print(value)` | `str` / `bool` / any integer | writes the value and a newline to standard output |
| `eprint(value)` | same | writes to standard error |
| `arg_count()` | `-> int` | number of command-line arguments, including the program name |
| `arg(index)` | `int -> str` | argument at `index`; empty string when out of range |
| `fs_read_text(path)` | `str -> FileReadResult` | reads a whole file |
| `fs_write_text(path, text)` | `str, str -> FileWriteResult` | replaces a whole file |
| `fs_write_text_atomic(path, text)` | `str, str -> FileWriteResult` | replaces a whole file, or leaves the old one |
| `fs_lock(path)` | `str -> FileWriteResult` | takes the advisory lock for `path` |
| `fs_unlock(path)` | `str -> FileWriteResult` | releases the advisory lock for `path` |
| `str_of_i64(value)` | `int -> str` | decimal representation |
| `str_of_bool(value)` | `bool -> str` | `"true"` or `"false"` |
| `parse_i64(text)` | `str -> ParseResult` | optional sign, surrounding ASCII whitespace ignored, overflow rejected |
| `str_find(haystack, needle)` | `str, str -> FindResult` | byte offset of the first occurrence |
| `str_slice(text, start, length)` | `str, int, int -> SliceResult` | copies a byte range |
| `str_trim(text)` | `str -> str` | removes leading and trailing ASCII whitespace (space, tab, newline, carriage return, vertical tab, form feed; not other Unicode spaces) |
| `list_push(list, value)` | `[T], T -> void` | appends `value` to `list` (§6.6) |
| `str_split(text, separator)` | `str, str -> [str]` | cuts `text` at every `separator`; an empty separator cuts between bytes |
| `str_join(parts, separator)` | `[str], str -> str` | concatenates `parts` with `separator` between them |

The result types are ordinary structs declared by the prelude, so their fields
are read like any other struct, and `let ... else` (§7.1) can unwrap them:

```gable
struct FileReadResult  { ok: bool, text: str, error: str, code: int }
struct FileWriteResult { ok: bool, error: str, code: int }
struct ParseResult     { ok: bool, value: int }
struct FindResult      { ok: bool, index: int }
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

`str_split` copies each piece, so the pieces stay valid after the text they came
from is gone, and `str_join` allocates the result once. Together they are how a
program turns a file into fields and fields back into a file: appending with `+`
in a loop copies the whole text on every step.

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
exclusively; while another program holds it, it retries for about two seconds and
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

1. It returns from `main`; the returned value, or 0 for a `void` `main`, is the
   exit status.
2. A runtime trap occurs. A trap writes `gable: runtime error: <message>` to
   standard error and exits with status 70. Traps are: integer division by zero,
   remainder by zero, and an out-of-bounds array or list index.
3. The process is terminated by the operating system.

Standard output is flushed before a trap exits, so output produced earlier is not
lost.

## 12. Memory

### 12.1 What a program says about memory

Nothing. Gable has no ownership annotations, no `free`, and no lifetime syntax.
Values of the fixed-size types live on the stack. Strings, list storage, and the
buffers behind `fs_read_text` live on a heap that the runtime manages:

* A value is reclaimed once the program can no longer reach it. Reaching means
  holding it in a variable, a field, an array element, a function argument, or a
  string that itself is reachable — the usual meaning of "in use".
* A value that is still reachable is never reclaimed, so a program that has been
  corrected for a use-after-free cannot appear to work and then fail later.
* Reclamation is automatic and driven by allocation: a program that allocates
  continuously is collected continuously, and a program that allocates nothing
  performs no collection work at all. There is no background thread and no timer.
* The cost is proportional to the memory that is still in use plus the memory
  that was allocated, not to the size of the program's history.

### 12.2 What this means for a program

```gable
fn main() {
    var total = 0
    for i in 0..1000000 {
        let piece = "${i},"        // a new string every iteration
        total = total + piece.len  // ...used once, then dropped
    }
    print(total)
}
```

The loop allocates a million strings. Peak memory does not grow with the number
of iterations: each iteration's string stops being reachable when the next one is
assigned, and is reclaimed. `tools/check-memory.sh` measures this: the workload
in `tools/bench/temporaries.gable` allocates 1.6 GiB in total and peaks at about
2.6 MiB.

### 12.3 Limits a program can rely on

* Reclamation is conservative: an integer that happens to equal the address of a
  heap block keeps that block alive until the value is gone. Peak memory is
  therefore the live set plus a bounded amount of pointer-shaped leftovers, not an
  exact figure. It never goes the other way: a reachable value is never reclaimed.
* Programs are single-threaded. Starting a thread would introduce a second stack
  that the collector does not scan, so the language has no way to start one, and
  adding concurrency requires extending the collector first (see `docs/decisions.md`, entry 0021).
* On a host that cannot report where the running thread's stack begins, the
  runtime does not collect at all. The program behaves exactly as specified but
  uses memory in proportion to what it allocated. Every host that CI builds on
  collects.
* Copying a value does not copy its memory: strings are a pointer and a length,
  list copies share their storage. `+` allocates and copies both operands into a
  new block, so building a string of length *n* one piece at a time costs *O(n²)*
  copying even though memory use stays bounded. Collecting the pieces in a `[str]`
  and joining them once allocates the result once and is the way to build text
  today; a mutable string builder is the next step in `docs/roadmap.md`.

## 13. Compiler diagnostics

Diagnostics are written to standard error in the form

```
path:line:column: severity: message
  line | source text
       | ^~~~~~
```

Every diagnostic names the file it belongs to, so a multi-file error is
unambiguous. Notes follow the error they explain. The compiler reports as many
independent errors as it can in one run and does not stop at the first, and it
does not report follow-on errors that only exist because the first one was made:
an expression that failed to check produces no further complaints about its type.

A diagnostic says what the programmer wrote, what Gable expected, and — when
there is a reliable fix — how to write it. The mistakes beginners make most often
have their own notes:

```
notes.gable:3:15: error: '=' assigns a value; compare with '==' in the condition of an 'if' statement
  3 |     if count = 3 {
    |              ^
  3 |     if count = 3 {
    |              ^ note: to assign, write the assignment as a statement of its own
```

```
notes.gable:4:10: error: 'len' is a value, not a method; write '.len' without '()'
```

```
notes.gable:7:5: error: type mismatch in argument: expected 'int', found 'i32'
    | note: Gable does not convert between number types on its own; write an explicit
    | conversion with 'as', for example 'value as int'
```

Compiler exit statuses: `0` success, `1` compilation failed, `2` usage error or
unusable output path, `70` internal failure (missing C toolchain, or generated C
that does not compile). `run` forwards the program's exit status, so a program
that exits with 1, 2, or 70 is indistinguishable from the corresponding compiler
failure.

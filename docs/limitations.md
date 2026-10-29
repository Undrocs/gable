# Implemented functionality and known limitations

This page is the honest inventory: what the compiler does today, what it does
badly, and what is missing. It is kept in step with the code. If you find a
behaviour that contradicts [specification.md](specification.md), that is a bug —
please report it.

## Implemented and tested

| Area | Status |
| --- | --- |
| Source handling | UTF-8 validated, CRLF accepted, NUL rejected, 64 MiB per-file limit, 512-file limit |
| Lexing | keywords, identifiers, decimal/hex/binary literals with 64-bit range check, strings with escapes, Unicode escapes and interpolation (`"$name"`, `"${expr}"`), `//` and nested `/* */` comments, `[` `]` `.` `..`, reserved `u` suffix and `&`/`|` diagnostics |
| Parsing | functions, structs, imports, `let`/`var` with optional `else` block, assignment, expression statements, `return`, `if`/`else if`/`else`, `while`, `for` over ranges, arrays, and lists, growable list types `[T]`, `break`, `continue`, nested blocks, array types and literals, field access, indexing, `as`, receiver calls (`text.trim()`, `items.push(x)`), full operator precedence, error recovery, 200-level nesting cap |
| Statement termination | a line break, `;`, `}`, or end of file ends a statement; `;` is accepted and not required |
| Semantics | one program-wide namespace across files, scopes and shadowing, duplicate detection, immutability and parameter rules, struct field resolution and containment-cycle detection, array and list type interning, literal contextual typing with range checks, `int` as the everyday 64-bit integer with `i8`–`u64` available, no implicit conversions, string operator rules, return-path analysis, loop control checks, `main` signature checks, builtin validation, fallible bindings checked against an `ok: bool` field |
| Diagnostics | `path:line:column: severity: message` naming the file, source line and caret, notes, multi-error runs, de-duplicated cascades, windowed rendering for very long lines, targeted notes for assignment in a condition, `.len()` written as a call, a field read as a method, missing number conversions, and undeclared assignment |
| Code generation | typed IR to C17, explicit left-to-right evaluation order with short-circuiting preserved, wrapping arithmetic, runtime division helpers, bounds checks, array wrapper structs, dependency-ordered struct typedefs, `--emit-c` |
| Toolchain | `build`, `run`, `check`, `--emit-c`, `--keep-temps`, `-O0/-O1/-O2`, `-v`, program arguments after `--`, runtime discovery, structured process execution, atomic outputs, input-alias refusal |
| Platforms | Linux with GCC and Clang (CI): verified. Windows with MSVC (CI): verified. macOS and ARM hosts use the POSIX implementation but are not verified or advertised |
| Runtime | `print`/`eprint` for `str`, `bool`, and all integer widths; string equality, concatenation, conversion, search, slicing, trimming, splitting and joining; growable lists with append, indexing, and iteration; arguments; whole-file I/O returning result structs; division and bounds traps with exit status 70; automatic reclamation of heap values |
| Formatting | `gable fmt` and `gable fmt --check`: whitespace-only, comment-preserving, idempotent, refused on a file that does not parse |
| Testing | 290 unit tests, 65 integration cases, 14 examples whose output is compared against `examples/expected/` and whose formatting `fmt --check` verifies, 83 notes-tool tests, 82 runtime file tests, 15 runtime memory tests, 10 runtime list tests, memory-limit measurement, sanitizer configuration |

## Known bugs and rough edges

* **Conservative reclamation overshoots.** Peak memory is the live set plus
  pointer-shaped leftovers on the stack, so a program can use somewhat more than
  it strictly needs. It is never less than the live set (specification §12.3).
* **Building a string in a loop copies it every time.** `text = text + piece` is
  *O(n²)* in copying because `str` is immutable; a collection of pieces joined
  once with `.join` is the way to build text today, and a mutable builder does not
  exist yet.
* **No maps or sets.** Lists grow, but there is no keyed collection; a program
  that needs one searches a list or writes its own structure.
* **Import nesting is capped at 64 levels.** Loading an import is recursive, and
  the cap is what keeps the loader's stack bounded; a program that chains
  imports deeper has to declare the shared files in the root file instead.
* **Exit status ambiguity under `run`.** A program returning 1, 2, or 70 is
  indistinguishable from a compiler failure by exit status alone.
* **No debug information for Gable source.** Debugging a generated executable means
  reading C-level line information.
* **Byte-oriented strings.** `.slice`, `.find`, and `.len` count bytes, not
  characters, and slicing can split a UTF-8 sequence. There is no character
  iteration API yet.
* **Long diagnostics on generated code.** When the C compiler rejects generated
  code, its own message is shown. It is accurate but refers to the generated file,
  not to Gable source.
* **No incremental builds.** Every `gable build` re-runs the frontend and the C
  compiler from scratch.
* **Temporary directory retention on failure.** Intentional: it leaves files in the
  temporary directory after an internal error so the generated C can be inspected.
* **Non-ASCII arguments on Windows** are converted from the process ANSI code page.
  On a system with a non-UTF-8 code page, arguments that the code page cannot
  represent are not recoverable.

## Missing language features

* Generics, traits, closures, first-class functions, overloading.
* Maps, sets, enums, tuples, unions, pointers, references.
* Bitwise and shift operators (`&` and `|` reserved).
* Character-level string iteration and a mutable string builder; formatting is
  interpolation plus `str_of_i64`/`str_of_bool`.
* Constants, compile-time evaluation, macros, attributes.
* Pattern matching, exceptions, `defer`/`finally`.
* Modules with visibility, package management, separate compilation.
* Concurrency and async: a second thread would need a stack the collector can
  see, so it cannot be added before the runtime supports it.
* A standard library beyond the built-ins in the specification.
* User-defined printing (no `print` of a struct or array).

## Missing engineering infrastructure

* No LLVM backend (see [decisions.md](decisions.md), entry 0001).
* No verification on macOS, ARM, or Windows with toolchains other than MSVC. CI
  runs Linux with GCC and Clang and Windows with MSVC; anything else is untested
  and not advertised.
* No fuzzing harness integrated into CI (robustness tests use deterministic
  pseudo-random inputs; libFuzzer/AFL integration is future work).
* No language server, editor grammar, or package manager. `gable fmt` exists and
  is whitespace-only by design (decisions.md, entry 0029); it does not re-wrap
  lines, sort imports, or split long expressions.
* No performance benchmarks or tracked compilation timings.
* No releases, no versioned artefacts, no package distribution.
* No self-hosting; the compiler is C++ and will stay that way for several
  milestones.

## Semantics that are deliberately chosen, not missing

These are specified decisions, not gaps:

* Integer overflow wraps; it does not trap.
* Division by zero is a compile-time error for constant zero and a runtime trap
  otherwise; the minimum value divided by -1 wraps.
* No implicit numeric conversions, including between integer widths; `as` is the
  only conversion and it is defined bit-wise.
* Conditions must be `bool`; there is no truthiness.
* A line break ends a statement; `;` is accepted for compatibility and ignored.
* Every binding has an initializer.
* `main` may be `fn main()` (exit status 0) or `fn main() -> int`.
* Evaluation order is left to right, specified by the language rather than
  inherited from the C backend.
* Structs and arrays are values: assignment, arguments, and returns copy.
* Structs cannot contain themselves by value.
* Recoverable failures (file I/O, parsing a number) return result structs instead
  of trapping, and `let ... else` handles them where the value is bound.

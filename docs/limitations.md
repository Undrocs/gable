# Implemented functionality and known limitations

This page is the honest inventory: what the compiler does today, what it does
badly, and what is missing. It is kept in step with the code. If you find a
behaviour that contradicts [specification.md](specification.md), that is a bug —
please report it.

## Implemented and tested

| Area | Status |
| --- | --- |
| Source handling | UTF-8 validated, CRLF accepted, NUL rejected, 64 MiB size limit |
| Lexing | keywords, identifiers, decimal/hex/binary literals with 64-bit range check, strings with escapes and Unicode escapes, `//` and nested `/* */` comments, reserved `u` suffix and `&`/`|` diagnostics |
| Parsing | functions, parameters, return types, `let`/`var`, assignment, expression statements, `return`, `if`/`else if`/`else`, `while`, nested blocks, empty statements, full operator precedence, error recovery, 200-level nesting cap |
| Semantics | scopes, shadowing, duplicate detection, immutability, literal contextual typing with range checks, no implicit conversions, integer-only arithmetic, boolean conditions, division-by-zero constant rejection, return-path analysis, `main` signature checks, `print` validation |
| Diagnostics | `path:line:column: severity: message`, source line and caret, multi-error runs, de-duplicated cascades, windowed rendering for very long lines |
| Code generation | typed IR to C17, wrapping arithmetic, runtime division helpers, pooled string literals, forward declarations, entry point wrapper, `--emit-c` |
| Toolchain | `build`, `run`, `--emit-c`, `--keep-temps`, `-O0/-O1/-O2`, `-v`, runtime discovery, structured process execution, RAII temporary directories |
| Runtime | `print` for `str`, `bool`, signed and unsigned integers; division-by-zero trap with exit status 70 |
| Testing | 142 unit tests, 34 integration cases, 6 examples, sanitizer configuration |

## Known bugs and rough edges

* **Exit status ambiguity under `run`.** A program returning 1, 2, or 70 is
  indistinguishable from a compiler failure by exit status. Output distinguishes
  them today; machine-readable CLI output is planned.
* **No debug information for Gable source.** Debugging a generated executable means
  reading C-level line information.
* **`evaluation order`** of operands in a binary expression is unspecified except
  for `&&`/`||` short-circuiting. Generated C mirrors that; do not write programs
  that depend on the order of side effects in one expression.
* **Long diagnostics on generated code.** When the C compiler rejects generated
  code, the C compiler's own message is shown. It is accurate but refers to the
  generated file, not to Gable source.
* **No incremental builds.** Every `gable build` re-runs the frontend and the C
  compiler from scratch.
* **Temporary directory retention on failure.** Intentional, but it leaves files in
  `TMPDIR` after an internal error so the generated C can be inspected.

## Missing language features

* Modules, imports, or multi-file programs.
* Arrays, records/structs, enums, tuples, pointers, references.
* Generics, traits, closures, first-class functions, overloading.
* Explicit conversions and casts.
* `break`, `continue`, `for`, `loop`, labelled control flow.
* Bitwise and shift operators (`&` and `|` reserved).
* String concatenation, comparison, indexing, length, formatting.
* Constants, compile-time evaluation, macros, attributes.
* Pattern matching, error handling (no `Result`/`try`), `defer`/`finally`.
* Any concurrency, async, or memory-management model beyond stack values.
* Any standard library beyond `print`.
* User-defined output formatting (no `print` of custom values).

## Missing engineering infrastructure

* No LLVM backend (see [decisions.md](decisions.md), entry 0001).
* No Windows support; the driver is POSIX-only and configuration fails early.
* No macOS or ARM verification has been performed by the project: the code uses
  portable C++20 and C17, but only Linux x86-64 has been built and tested here.
* No fuzzing harness integrated into CI (robustness tests use deterministic
  pseudo-random inputs; libFuzzer/AFL integration is future work).
* No formatter, linter, language server, editor grammar, or package manager.
* No performance benchmarks or tracked compilation timings.
* No releases, no versioned artefacts, no package distribution.
* No self-hosting; the compiler is C++ and will stay that way for several
  milestones.

## Semantics that are deliberately chosen, not missing

These are specified decisions, not gaps:

* Integer overflow wraps; it does not trap.
* Division by zero is a compile-time error for constant zero and a runtime trap
  otherwise.
* `INT_MIN / -1` wraps.
* No implicit numeric conversions; literals take their type from context.
* Conditions must be `bool`; there is no truthiness.
* Semicolons are required; newlines are whitespace.
* `main` must be `fn main() -> i32`.

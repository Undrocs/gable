# Roadmap

Milestones are ordered. Each has an acceptance criterion that can be verified in
this repository; nothing is listed as complete unless the tests exist and pass.
Ideas that are not scheduled appear under "Not planned yet" and are not promises.

## Milestone 0 — Foundation · complete

* C++20/C17/CMake/Ninja toolchain, warning-clean builds.
* `gable` CLI with `--help`, `--version`, `build`, `run`.
* Apache-2.0 licensing, contribution and security policies.
* CTest suites wired up for unit and integration tests.

Verified by: `ctest` passes in Debug, Release, and sanitizer configurations.

## Milestone 1 — Frontend · complete

Lexer, tokens, source locations, parser, AST, diagnostics, unit tests.

Verified by: lexer/parser unit tests, including precedence, error recovery, and
malformed input; integration cases that assert exact diagnostic text.

## Milestone 2 — Semantics · complete

Scopes, symbol resolution, builtin types, function signatures, type checking,
return-path validation, semantic tests.

Verified by: semantic unit tests for undefined names, duplicates, immutability,
type mismatches, arity, literal ranges, `main` validation, and multi-error runs.

## Milestone 3 — First native executable · complete

Typed IR, C17 backend, runtime integration, object generation and linking through
the host C compiler, `--emit-c` diagnostics mode.

Verified by: integration cases that build an executable, run it directly, and
compare output and exit status.

## Milestone 4 — Usable experimental language · complete

Functions, variables, branches, loops, strings, standard output, examples,
`gable build`, `gable run`, expanded end-to-end tests.

Verified by: 34 integration cases plus 6 example programs executed on every test
run at the time; the suites have since grown to 53 cases and 10 examples.

## Milestone 5 — Comfortable to use (language 0.2) · complete

Review of 0.1 identified correctness gaps before features, and usability gaps
before new surface area.

* Output safety: no output path may overwrite an input, including through
  symlinks and hard links; outputs are written to a temporary file and replaced in
  one step; a failed build never damages the previous executable.
* Defined evaluation order for call arguments and non-short-circuit operands, with
  `&&`/`||` still short-circuiting.
* Multi-file programs with a minimal `import` model: cycles rejected, each file
  parsed once, one program-wide namespace.
* Value structs and fixed-size arrays with bounds-checked element access.
* `str` with an explicit length, equality, concatenation, byte length, and library
  operations; bytes versus characters documented.
* Explicit integer conversions with defined narrowing, `break`, `continue`, and
  `for` over ranges and arrays.
* Command-line arguments and whole-file input/output with typed result structs.
* `gable check`, clear exit statuses, and help text.
* A native Windows implementation (wide-character paths, `CreateProcessW`,
  temporary-file output capture, MSVC option syntax) verified by a CI job.
* An acceptance program: `tools/notes`, a multi-file task list that takes
  arguments, uses structs and strings, and reads and writes a text file.

Verified by: 215 unit tests, 58 integration cases, 10 examples, 83 notes-tool
tests, and 79 runtime file tests, all passing in Debug and under
AddressSanitizer/UBSan on Linux, plus the compiler suites on Windows with MSVC.

## Milestone 6 — LLVM backend

Replace the C emitter as the default backend while keeping it available as
`--backend=c`.

* Initialise LLVM and configure the target triple and data layout from the host.
* Lower `ir::Stmt`/`ir::Expr` to functions, basic blocks, and `alloca`-based locals.
* Implement wrapping arithmetic with `add`/`sub`/`mul`, division with explicit
  zero checks and `sdiv`/`srem`, comparisons with `icmp`, and `br` for control flow.
* Emit object files and link with the platform linker (LLD where available).
* Keep `--emit-llvm` as a diagnostic option.

Acceptance: `gable build` with `--backend=llvm` produces executables that pass the
same integration suite; the suite runs against both backends.

Requires: LLVM development files on the build host. The build must fail with a
clear message when `GABLE_WITH_LLVM=ON` but LLVM is missing.

## Milestone 7 — Language and standard library

Each item requires a specification update, implementation, and tests before it is
merged.

* Dynamic arrays and maps, which require the memory model below.
* A memory model: ownership and reclamation of run-time strings and containers.
* Character-level text operations and formatting.
* Enums, tuples, and a sum type for error handling.
* Bitwise and shift operators.
* A `stdlib` written in Gable wherever the language allows, with C only for
  platform boundaries.

Acceptance: a program that needs growable collections and bounded memory use can be
written without workarounds, and the specification documents every construct
used.

## Milestone 8 — Developer experience

* `gable fmt` with a stable formatting policy.
* Tree-sitter grammar and editor support.
* Language server reusing `gable_compiler` (diagnostics, hover, go-to-definition).
* Debug information for Gable source, mapped through the backend.
* Release process and documented versioning.

Acceptance: a new contributor can build the toolchain, run the examples, and land
a tested change by following `CONTRIBUTING.md`.

## Milestone 9 — Self-hosting

Long-term, not scheduled.

* Grow the language until the compiler's data structures (maps, vectors, strings)
  are expressible.
* Port individual compiler components to Gable, starting with a tool that exercises
  the standard library.
* Establish a reproducible bootstrap chain and keep the C++ compiler as the
  reference implementation until the Gable compiler builds itself twice with
  identical results.

Acceptance: a Gable-written compiler builds itself from a fixed bootstrap binary,
passes the full test suite, and the bootstrap chain is documented.

## Not planned yet

Generics, traits, pattern matching, ownership/borrowing, garbage collection,
async/await, threads, macros, reflection, package registry, GUI frameworks, GPU
compilation, kernel support.

Each of these is a significant design problem. They will be considered when the
language has a stable base and a demonstrated need, not because another language
has them.

## GitHub Linguist recognition

Gable is not recognised by GitHub Linguist. Linguist requires a TextMate grammar
and sustained usage across roughly 200 public repositories before a language entry
is accepted. The metadata Gable intends to propose is recorded in
[linguist.md](linguist.md); a pull request will not be opened before the
preconditions are genuinely met.

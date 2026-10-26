# Compiler architecture

This document describes how the Gable compiler is organised, the reasoning
behind the current pipeline, and where the boundaries between components lie.

## Pipeline

```
 .gable source
      |
      v
  SourceFile  ─────────── UTF-8 validation, line index, byte spans
      |
      v
   Lexer      ─────────── Token stream with byte spans, decoded literals
      |
      v
   Parser     ─────────── Recursive descent + Pratt expressions -> ast::Program
      |
      v
 SemanticAnalyzer ────── Scopes, name resolution, types -> ir::Module
      |
      v
  CBackend    ─────────── C17 translation unit
      |
      v
  Host C compiler (cc) ── native object code and linking
      |
      v
  libgable_rt.a ───────── Gable runtime (C17)
      |
      v
   Native executable
```

The pipeline is implemented as a library (`gable_compiler`) plus a thin
command-line front end (`gable`, in `compiler/src/cli/main.cpp`). The library has
no global state and no I/O beyond the caller-provided streams and explicit file
paths, so tests and future tooling can drive it directly.

## Components and files

| Path | Responsibility |
| --- | --- |
| `compiler/include/gable/source.h`, `src/lexer/source.cpp` | Source file, UTF-8 aware line/column mapping |
| `compiler/include/gable/diagnostics.h`, `src/semantic/diagnostics.cpp` | Diagnostic collection and rendering with source context |
| `compiler/include/gable/token.h`, `lexer.h`, `src/lexer/lexer.cpp` | Token kinds and handwritten lexer |
| `compiler/include/gable/operators.h`, `src/types/operators.cpp` | Operator enums shared by the AST, IR, and backends |
| `compiler/include/gable/ast.h`, `src/parser/parser.cpp` | Untyped syntax tree and parser |
| `compiler/include/gable/ir.h`, `src/types/ir.cpp` | Typed IR: resolved types, slots, callee indices |
| `compiler/include/gable/sema.h`, `src/semantic/sema.cpp` | Name resolution, type checking, lowering to IR |
| `compiler/include/gable/c_backend.h`, `src/codegen/c_backend.cpp` | IR to C17 |
| `compiler/include/gable/compiler.h`, `src/driver/frontend.cpp` | Frontend orchestration |
| `compiler/include/gable/toolchain.h`, `src/driver/toolchain.cpp` | Runtime discovery, C compilation, linking |
| `compiler/include/gable/process.h`, `src/driver/process.cpp` | Structured child processes (no shell) |
| `compiler/include/gable/temporary.h`, `src/driver/temporary.cpp` | Private temporary directories |
| `compiler/include/gable/cli.h`, `src/cli/cli.cpp` | Argument parsing, command dispatch, exit codes |
| `runtime/include/gable/rt.h`, `runtime/src/*.c` | C17 runtime with a C ABI |

## Lexer

A single pass over the source. It records byte spans rather than copying text
where possible, validates UTF-8 up front so that locations remain trustworthy,
and reports lexical problems without stopping, so one run can report several.
Nested block comments are supported. Integer literals are decoded during
scanning with explicit overflow checks. String escapes are decoded to UTF-8 bytes
at compile time; NUL escapes are rejected because `str` is NUL-terminated.

## Parser

Recursive descent for declarations, statements, and blocks; Pratt parsing for
expressions with the precedence table in the specification. The parser produces
`ast::Program` and never throws. Errors are reported at the offending token and
recovery synchronises at `;`, `}`, or the next statement/declaration keyword.

Hostile input is a design consideration: nesting depth for blocks and expressions
is capped (200 levels). Once the cap is reached the parser stops, which bounds
both stack usage and memory; the cap prevents the unbounded statement growth that
a naive recovery loop would produce on deeply nested braces.

## Semantic analysis

Semantic analysis walks the AST once and produces a typed module:

* Scopes are a stack of name-to-binding maps; function names live in a separate
  program-wide namespace so forward and recursive calls resolve.
* Expression typing is context-directed: `checkExpr(expr, expected)` passes the
  type demanded by the surrounding context, which is what gives integer literals
  a type without suffix syntax or a constraint solver. Non-literal values are
  never converted implicitly.
* Failures produce `ir::Type` "error", which suppresses follow-on diagnostics so a
  single mistake does not cascade.
* Locals are resolved to numeric slots, and calls to function indices, so the
  backend performs no lookups.

Type checking is deliberately separate from parsing: the parser accepts any
identifier where a type name is expected so that unknown types are reported as
`unknown type 'x'` with a span, rather than as a syntax error.

## Typed IR

`ir::Module` contains functions with resolved signatures, a flat local table, and
structured statements. Keeping the IR structured (rather than lowering to a CFG)
is a v0.1 decision: the language has no `break`, `continue`, or early-exit
constructs, so structured forms are sufficient, and both current and future
backends can lower them directly.

The IR is the contract between the frontend and code generation. A second
backend (for example LLVM IR) consumes the same `ir::Module` and needs no
knowledge of Gable syntax or name resolution.

## C backend

The backend emits one C17 translation unit per program:

* Names are prefixed (`gable_fn_<name>`, `gable_v<slot>_<name>`, `gable_str_<n>`) to
  avoid collisions with the runtime and the platform C library.
* Signed arithmetic is performed in the matching unsigned type and converted
  back, so Gable's defined wrapping overflow never becomes C undefined behaviour.
* Division and remainder call runtime helpers, which trap on a zero divisor and
  define `INT_MIN / -1` as wrapping.
* Every subexpression is parenthesised; the output never depends on C precedence.
* String literals are pooled per translation unit and escaped with fixed-width
  octal escapes, except for `\"` and `\\`, which stay readable.

Generated C is compiled with `-std=c17 -Wall -Wextra`; the integration suite fails
if the C compiler emits any warning, so backend regressions surface as test
failures rather than as silent noise.

## Driver, processes, and files

`buildExecutable` runs the frontend, resolves the runtime, writes the C file into a
private directory created with `mkdtemp`, invokes the C compiler with an argument
vector (never a shell), and reports failures. On success the directory is removed;
on an internal failure it is retained and its path is printed, because that means
a compiler defect and the generated C is the evidence.

Runtime location is resolved in order: explicit options, `GABLE_RUNTIME_INCLUDE`
and `GABLE_RUNTIME_LIB`, build-tree paths recorded at configure time, then
install-tree paths. A candidate is only accepted if the files exist, so stale
compile-time paths never produce confusing linker errors.

## Runtime

The runtime is C17 with a C ABI. It owns standard output formatting and integer
division traps; it contains no language semantics that the type checker should
know about. It is linked statically into every executable, so a produced binary
does not depend on the Gable installation at run time.

## Testing

* `tests/unit` — lexer, parser, semantics, C emission, CLI behaviour, and
  robustness (random token soup, random bytes, mutations of valid programs,
  nesting limits). Deterministic seeds only.
* `tests/integration` — data-driven cases: each directory holds a program and
  expected stdout, exit status, and diagnostic substrings. Successful cases are
  executed both through `gable run` and through `gable build` plus direct
  execution of the produced binary. Examples under `examples/` must compile,
  run, and exit with status 0.
* `build/asan` (CMake preset `asan`) runs the same suites under AddressSanitizer
  and UndefinedBehaviorSanitizer.

## Known structural limitations

* Windows is not supported yet: the driver relies on POSIX `fork`/`exec`, and
  configuration fails early with an explanation rather than a half-working port.
* There is no incremental compilation or caching; each invocation re-runs the
  whole pipeline.
* The C backend is not (and cannot be) a full replacement for LLVM: it inherits
  the platform C compiler's optimisation quality and code-generation model. See
  [decisions.md](decisions.md) for the rationale and the migration path.

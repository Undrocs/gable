# Compiler architecture

This document describes how the Gable compiler is organised, the reasoning
behind the current pipeline, and where the boundaries between components lie.

## Pipeline

```
 root .gable file
      |
      v
   Loader     ──────────── imports resolved, every file parsed once, cycles rejected
      |                    (each file: SourceFile -> Lexer -> Parser -> ast::Program)
      v
 SemanticAnalyzer ─────── one program-wide namespace, types, lowering -> ir::Module
      |                    (the prelude is analysed first: it declares builtin results)
      v
  CBackend    ─────────── one C17 translation unit per program
      |
      v
  Host C compiler ─────── native object code and linking
      |                    (cc / clang / cl, chosen per platform)
      v
  libgable_rt.a ───────── Gable runtime (C17)
      |
      v
   Native executable
```

The pipeline is implemented as a library (`gable_compiler`) plus a thin
command-line front end (`gable`, in `compiler/src/cli/main.cpp`). The library has
no global state and no I/O beyond the caller-provided streams and explicit file
paths, so tests and future tooling drive it directly.

## Components and files

| Path | Responsibility |
| --- | --- |
| `compiler/include/gable/source.h`, `src/lexer/source.cpp` | Source file, UTF-8 aware line/column mapping |
| `compiler/include/gable/diagnostics.h`, `src/semantic/diagnostics.cpp` | Multi-file diagnostic collection and rendering |
| `compiler/include/gable/token.h`, `lexer.h`, `src/lexer/lexer.cpp` | Token kinds and handwritten lexer |
| `compiler/include/gable/operators.h`, `src/types/operators.cpp` | Operator enums shared by the AST, IR, and backends |
| `compiler/include/gable/ast.h`, `src/parser/parser.cpp` | Untyped syntax tree and parser |
| `compiler/include/gable/loader.h`, `src/driver/loader.cpp` | Import resolution, cycle detection, per-file parse |
| `compiler/include/gable/prelude.h` | Declarations of builtin result structs, written in Gable |
| `compiler/include/gable/ir.h`, `src/types/ir.cpp` | Typed IR: resolved types, slots, callee indices, array types |
| `compiler/include/gable/sema.h`, `src/semantic/sema.cpp` | Name resolution, type checking, lowering to IR |
| `compiler/include/gable/c_backend.h`, `src/codegen/c_backend.cpp` | IR to C17, including explicit evaluation-order sequencing |
| `compiler/include/gable/compiler.h`, `src/driver/frontend.cpp` | Frontend orchestration (`analyzeSourceFile`) |
| `compiler/include/gable/toolchain.h`, `src/driver/toolchain.cpp` | Runtime discovery, C compiler invocation, linking |
| `compiler/include/gable/output.h`, `src/driver/output.cpp` | Input-alias checks and atomic output replacement |
| `compiler/include/gable/platform.h`, `src/platform/{posix,windows}.cpp` | Paths, process id, temporary directories, executable suffix, file replacement |
| `compiler/include/gable/process.h`, `src/driver/process_{posix,windows}.cpp` | Structured child processes (no shell) |
| `compiler/include/gable/temporary.h`, `src/driver/temporary.cpp` | Private temporary directories |
| `compiler/include/gable/cli.h`, `src/cli/cli.cpp` | Argument parsing, command dispatch, exit codes |
| `runtime/include/gable/rt.h`, `runtime/src/*.c` | C17 runtime with a C ABI |
| `runtime/src/gc.c`, `runtime/src/stack_top.c` | Heap for `str`, lists, and file buffers; conservative mark-and-sweep collector; each host's stack bounds |
| `runtime/src/list.c` | Growable lists: storage that doubles, element stride chosen by the compiler |

## Loading and imports

`loadProgram` walks the import graph depth-first. Each file is canonicalized
(`weakly_canonical`) so that a file imported through different paths, or through a
symlink, is loaded once. The loader keeps a stack of files being loaded; meeting a
file already on the stack is a cycle, reported with the chain that closes it.

Files are parsed as they are discovered and appended to the result *after* their
imports, so the analysis order is deterministic and dependencies always precede
their dependents. A syntax error in any file sets `hasSyntaxErrors`, and semantic
analysis is skipped: an incomplete tree produces misleading follow-on errors.

The loader also owns the size limit (64 MiB per file) and the file-count limit
(512), which keep a hostile or accidental import structure from consuming the
machine.

## Runtime memory

The runtime owns every value the language cannot put on the stack: the bytes
behind `str`, and the buffer `fs_read_text` returns. It has one allocator,
`gable_rt_allocate` in `runtime/src/gc.c`. Nothing in the runtime or in generated
code frees a block; a block becomes garbage when the program stops referring to it,
and the collector reclaims it during a later allocation. That keeps the ownership
question out of the runtime's API and out of the language.

The collector traces from two roots: the machine stack, scanned from the current
stack pointer up to the base of the thread's stack, and the callee-saved registers,
copied out with `setjmp`. A word is treated as a pointer when it falls anywhere
inside a block the runtime allocated, including its header, so slices that point
into the middle of a string keep that string alive. Blocks are held in a table
sorted by address, which makes resolving a candidate pointer a binary search.

Two details matter for correctness and are easy to get wrong:

* *Where the stack ends.* The upper bound cannot be the address of a local in the
  generated entry point: the compiler may place other locals above it, and
  AddressSanitizer does. `runtime/src/stack_top.c` asks the host for the thread's
  stack bounds instead. When a host cannot answer, the collector stays off rather
  than guessing, and the program simply keeps its memory.
* *What a collection is allowed to see.* AddressSanitizer poisons the redzones
  between stack variables, and a conservative scan walks through them. The scan
  skips a word that is not fully addressable, which cannot lose a live pointer but
  keeps sanitizer builds usable.

Lists are handles to a heap block that holds the elements, the count, the capacity,
and the element stride. The compiler knows the element type, so one runtime
implementation serves every element type: the generated code passes addresses of
elements and copies them back out with `memcpy`. A list element is therefore not a
C lvalue, and an assignment whose target is rooted in a list element is lowered to
a `memcpy` into the storage the runtime returns rather than to a C assignment.

`gable_rt_init()` is called first in the generated entry point. It records the
stack top, enables the collector, and registers an `atexit` handler that frees
everything, so that leak checkers report leaks in the program rather than in the
runtime.

## Prelude

Builtin functions need result types, and inventing a second type syntax for them
would be confusing. Instead the result structs (`FileReadResult`, `ParseResult`,
and so on) are Gable source in `prelude.h`, compiled with file index 0 before any
user file. The language therefore sees ordinary structs with ordinary field
access, while code generation uses the matching C definitions in `gable/rt.h`. The
field order of each struct is part of that ABI.

## Lexer

A single pass over the source. It records byte spans rather than copying text
where possible, validates UTF-8 up front so locations remain trustworthy, and
reports lexical problems without stopping. Nested block comments are supported.
Integer literals are decoded during scanning with explicit overflow checks. String
escapes are decoded to UTF-8 bytes at compile time; NUL escapes are rejected
because Gable strings never contain NUL.

## Parser

Recursive descent for declarations, statements, and blocks; Pratt parsing for
expressions with the precedence table in the specification, including postfix
`[]`, `.`, and `as`. The parser produces `ast::Program` and never throws. Errors are
reported at the offending token and recovery synchronises at `;`, `}`, or the next
statement/declaration keyword.

Nesting depth for blocks and expressions is capped at 200 levels. Once the cap is
reached the parser stops, which bounds both stack usage and memory; the cap
prevents the unbounded statement growth that a naive recovery loop produced on
deeply nested braces.

## Semantic analysis

Semantic analysis collects all declarations of all files before checking any body,
then walks each body once:

* Scopes are a stack of name-to-binding maps. Functions and structs live in a
  program-wide namespace; locals and parameters shadow nothing outside their
  block.
* Expression typing is context-directed: `checkExpr(expr, expected)` passes the
  type demanded by the surrounding context, which is what gives integer literals a
  type without suffix syntax or a constraint solver. Non-literal values are never
  converted implicitly.
* Struct fields are resolved after every struct exists, so a struct may refer to
  another struct declared later or in another file. A value-containment cycle is
  reported before any layout is computed.
* Failures produce `ir::Type` "error", which suppresses follow-on diagnostics so a
  single mistake does not cascade.
* Locals are resolved to numeric slots, calls to indices into the module's function
  vector, and array types to interned type-table entries.

Type checking is deliberately separate from parsing: the parser accepts any
identifier where a type name is expected so that unknown types are reported as
`unknown type 'x'` with a span, rather than as a syntax error.

## Typed IR

`ir::Module` holds the struct table, the interned array types, and functions with
resolved signatures, a flat local table, and structured statements (`if`,
`while`, `for` over a range or an array, `break`, `continue`, `return`).

Keeping the IR structured rather than lowering to a CFG keeps the backend small
and readable; every construct maps to a C statement. The IR is the contract
between frontend and code generation: a second backend consumes the same
`ir::Module` and needs no knowledge of Gable syntax or name resolution.

## C backend

The backend emits one C17 translation unit per program:

* Array types become wrapper structs (`typedef struct { T data[N]; } gable_arr_k`),
  so array values can be assigned, passed, and returned by value in C.
* Struct typedefs are emitted in dependency order; the prelude's structs come from
  `gable/rt.h` and are not redeclared.
* Names are prefixed (`gable_fn_<name>`, `v<slot>_<name>`) so user identifiers
  cannot collide with the runtime or the C library.
* Signed arithmetic is performed in the matching unsigned type and converted back,
  so Gable's defined wrapping overflow never becomes C undefined behaviour.
* Division and remainder call runtime helpers, which trap on a zero divisor and
  define `MIN / -1` as wrapping. Other integer widths are widened through
  `int64_t`/`uint64_t` and narrowed again, which gives the same results because the
  operands fit.
* Every subexpression is parenthesised; the output never depends on C precedence.

### Evaluation order

C does not specify the order in which function arguments or most binary operands
are evaluated. The emitter therefore gives every expression with an observable
effect — a call, an allocation, a trap, an index check, an aggregate construction
— a name and emits it as a statement before the next operand is evaluated:

```c
int32_t t0 = gable_fn_left();
int32_t t1 = gable_fn_right();
int32_t t2 = gable_fn_pair(t0, t1);
```

Pure expressions (literals, locals, field reads) are used inline, so this costs
nothing after the C compiler optimizes the temporaries away. `&&` and `||` keep
short-circuiting: when an operand has effects the emitter produces an `if`
statement instead of a temporary, so the right operand is evaluated only when the
result is not already determined.

## Driver, processes, and files

`buildExecutable` runs the frontend, checks the destination against every input
file of the compilation (through symlinks and hard links), resolves the runtime,
writes the C file into a private temporary directory, invokes the C compiler with
an argument vector (never a shell), and replaces the destination atomically.

`platform.h` is the only place that knows about the host. It provides the
executable path, the process id, temporary directory creation, the executable
suffix (`.exe` on Windows), the default C compiler, and atomic file replacement.
`process.h` starts child processes: POSIX uses `fork`/`exec` with pipes, Windows
uses `CreateProcessW` with Microsoft-runtime argument quoting and temporary files
for captured output.

The C compiler is invoked in one of two styles: GCC/Clang options
(`-std=c17 -O<n> -Wall -Wextra -I ... -o ...`) or MSVC/clang-cl slash options
(`/std:c17 /O<n> /W4 /I... /Fo... /Fe...`). The family is chosen from the compiler
name. The default is `cc` on POSIX hosts and `cl` on Windows.

Runtime location is resolved in order: explicit options, `GABLE_RUNTIME_INCLUDE`
and `GABLE_RUNTIME_LIB`, build-tree paths recorded at configure time, then
install-tree paths. A candidate is only accepted if the files exist, so stale
compile-time paths never produce confusing linker errors.

## Runtime

The runtime is C17 with a C ABI: output formatting, string operations, argument
conversion, whole-file I/O, file locking, integer division, and traps. It contains
no language semantics that the type checker should know about, and is linked
statically into every executable. File failures are returned as values with a
stable code, never as localised text.

Strings and list storage are allocated on a heap the runtime manages: a value is
reclaimed once the program can no longer reach it, which is a documented property
of the language (specification §12) rather than an implementation detail; see
[decisions.md](decisions.md) ADR 0021.

## Testing

* `tests/unit` — lexer, parser, semantics (including multi-file programs written
  to temporary directories), C emission, CLI behaviour, and robustness (random
  token soup, random bytes, mutations of valid programs, nesting limits).
* `tests/integration` — data-driven cases: each directory holds a program,
  optional helper files, optional program arguments, expected stdout, exit status,
  and diagnostic substrings. Successful cases run both through `gable run` and
  through `gable build` plus direct execution of the produced binary. Examples
  under `examples/` must compile, run, and exit with status 0.
* `tools/notes/tests` — the acceptance test: builds a real multi-file program and
  drives it through its command line and its file.
* `runtime/tests` — calls the runtime's file builtins from C, where a NUL byte in a
  path and a file above the read limit can actually be constructed. POSIX only.
* `build/asan` (CMake preset `asan`) runs the same suites under AddressSanitizer
  and UndefinedBehaviorSanitizer.

## Known structural limitations

* There is no incremental compilation or caching; each invocation re-runs the whole
  pipeline.
* The C backend is not a replacement for LLVM: it inherits the platform C
  compiler's optimisation quality and code-generation model. See
  [decisions.md](decisions.md) for the rationale and the migration path.
* Only Linux (GCC and Clang) and Windows (MSVC) are verified by CI. macOS and ARM
  hosts are expected to work through the POSIX implementation but are not
  advertised as supported.

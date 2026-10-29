# Gable

Gable is a statically typed, natively compiled, general-purpose programming
language developed by Undrocs.

**Status: experimental, pre-1.0.** Gable compiles a small, deliberately limited
language to native executables today. Its syntax and semantics will change. It is
not ready for general use.

```gable
fn add(a: int, b: int) -> int {
    return a + b
}

fn main() {
    let result = add(20, 22)
    print("Hello from Gable!")
    print("20 + 22 = $result")
}
```

```
$ gable run hello.gable
Hello from Gable!
20 + 22 = 42
```

## What works

* `.gable` source files, UTF-8 input, line/column diagnostics that name the file,
  including multi-file programs.
* Handwritten lexer and a recursive-descent parser with Pratt expression parsing.
* Semantic analysis: one program-wide namespace across imported files, lexical
  scopes, shadowing, duplicate detection, `let`/`var` immutability, return-path
  checking.
* Types: `int` (64-bit) as the everyday number, the narrower `i8`–`u64` widths
  for data that has a width, `bool`, `str`, `void`, value structs, fixed-size
  arrays, and growable lists.
* Arithmetic (wrapping, defined), comparison, boolean operators with
  short-circuiting, explicit `as` conversions with defined narrowing.
* Statements that end at a line break (`;` is still accepted), functions with
  inferred or annotated types, recursion, conditionals, `while`, `for` over
  ranges, arrays, and lists, `break`, `continue`, nested blocks.
* Strings with an explicit length: concatenation, interpolation (`"hello $name"`,
  `"${count + 1}"`), equality, byte length, search, slicing, trimming, splitting,
  joining, and number conversion, with the string operations written on the value
  (`text.trim().split(",")`).
* Growable lists (`[T]`) with append, indexing, and iteration, and `let value =
  fallible() else { ... }` for handling a failure where the value is bound.
* Command-line arguments and whole-file text input and output that report failures
  as values instead of trapping.
* Native executables through a typed IR and a C17 backend, plus `--emit-c` for
  inspecting the generated code.
* `gable build`, `gable run`, and `gable check`, with defined exit statuses and
  atomic, input-safe output handling.
* 294 unit tests, 65 end-to-end cases, 14 example programs whose printed output
  is compared against `examples/expected/`, 83 acceptance tests for the notes
  tool, and 107 runtime tests — run in Debug, Release, and AddressSanitizer/UBSan
  configurations.

## Platform support

| Host | Toolchain | Status |
| --- | --- | --- |
| Linux x86-64 | GCC, Clang | verified in CI |
| Windows x64 | MSVC | verified in CI |
| macOS, ARM hosts | GCC/Clang via the POSIX implementation | expected to work, not verified, not advertised |

Only the platforms in the first two rows are advertised as supported, because CI
runs on them. Everything else is untested.

## What does not work yet

No generics, traits, closures, pointers, enums, maps, or exceptions. There is no
formatter, language server, editor grammar, or package manager. Strings built at
run time are reclaimed automatically, but text built with `+` in a loop is still
copied every time: collect the pieces in a `[str]` and join them once. The backend
emits C17 and uses the host C compiler; there is no LLVM backend yet, because no
LLVM development files were available when the first backend was written.
[docs/decisions.md](docs/decisions.md) explains that decision and the migration
path in detail.

See [docs/limitations.md](docs/limitations.md) for the full list and
[docs/roadmap.md](docs/roadmap.md) for what is planned.

## Requirements

* C++20 compiler (GCC 12+ or Clang 15+)
* C17 compiler (used by the backend)
* CMake 3.20+
* Ninja (recommended)
* Linux or Windows (the C toolchain above is what the backend invokes)

No third-party libraries are needed to build the compiler.

## Build

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Without presets:

```bash
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

## Compile and run

```bash
./build/debug/gable build hello.gable -o hello   # produces an executable
./build/debug/gable run hello.gable              # compiles and executes
./build/debug/gable check hello.gable            # type checks, writes nothing
./build/debug/gable build hello.gable --emit-c   # shows the generated C17
./build/debug/gable run tools/notes/notes.gable -- list
```

`gable --help` lists every option. [docs/getting-started.md](docs/getting-started.md)
documents the build, the CLI, environment variables, and troubleshooting.

## Documentation

| Document | Contents |
| --- | --- |
| [docs/specification.md](docs/specification.md) | Language specification for version 0.3 |
| [docs/architecture.md](docs/architecture.md) | Compiler pipeline and component responsibilities |
| [docs/decisions.md](docs/decisions.md) | Design decisions, rejected alternatives, revisit triggers |
| [docs/getting-started.md](docs/getting-started.md) | Build, install, CLI reference, exit statuses |
| [docs/roadmap.md](docs/roadmap.md) | Milestones with verifiable acceptance criteria |
| [docs/limitations.md](docs/limitations.md) | Implemented functionality versus gaps |
| [docs/linguist.md](docs/linguist.md) | GitHub language statistics, Linguist status, and naming |

## Repository layout

```
compiler/        C++20 compiler library and the `gable` CLI
  include/gable/ public headers
  src/           lexer, parser, ast, semantic, types, codegen, driver, cli
runtime/         C17 runtime linked into every executable
examples/        example programs, exercised by the test suite
tools/notes/     the acceptance program: a task list written in Gable
tests/unit/      unit tests (lexer, parser, semantics, codegen, CLI, robustness)
tests/integration/ data-driven end-to-end cases
docs/            specification, architecture, decisions, roadmap
```

## Contributing

Contributions are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) first: it covers
the build, the test requirements, and how language changes are proposed. Security
issues should follow [SECURITY.md](SECURITY.md). Participation is governed by
[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE).

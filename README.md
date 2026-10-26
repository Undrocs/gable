# Gable

Gable is a statically typed, natively compiled, general-purpose programming
language developed by Undrocs.

**Status: experimental, pre-1.0.** Gable compiles a small, deliberately limited
language to native executables today. Its syntax and semantics will change. It is
not ready for general use.

```gable
fn add(a: i64, b: i64) -> i64 {
    return a + b;
}

fn main() -> i32 {
    let result: i64 = add(20, 22);
    print("Hello from Gable!");
    print(result);
    return 0;
}
```

```
$ gable run hello.gable
Hello from Gable!
42
```

## What works

* `.gable` source files, UTF-8 input, line/column diagnostics with source context.
* Handwritten lexer and a recursive-descent parser with Pratt expression parsing.
* Semantic analysis: lexical scopes, shadowing, name resolution, duplicate
  detection, `let`/`var` immutability, return-path checking.
* Types: `i8` `i16` `i32` `i64`, `u8` `u16` `u32` `u64`, `bool`, `str`, `void`.
* Arithmetic (wrapping, defined), comparison, boolean operators with
  short-circuiting, unary `-` and `!`.
* Functions, parameters, recursion, mutual recursion, conditionals, `while` loops,
  nested blocks, `print`.
* Native executables through a typed IR and a C17 backend, plus `--emit-c` for
  inspecting the generated code.
* `gable build` and `gable run`, with defined exit statuses.
* 142 unit tests and 34 end-to-end cases plus 6 example programs, run in Debug,
  Release, and AddressSanitizer/UBSan configurations.

## What does not work yet

No modules, arrays, records, generics, closures, pointers, casts, `break`/
`continue`, string operations, or standard library beyond `print`. Windows is not
supported. There is no LLVM backend yet: the current backend emits C17 and uses the
host C compiler, because no LLVM development files were available when it was
written. [docs/decisions.md](docs/decisions.md) explains that decision and the
migration path in detail.

See [docs/limitations.md](docs/limitations.md) for the full list and
[docs/roadmap.md](docs/roadmap.md) for what is planned.

## Requirements

* C++20 compiler (GCC 12+ or Clang 15+)
* C17 compiler (used by the backend)
* CMake 3.20+
* Ninja (recommended)
* Linux or macOS

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
./build/debug/gable build hello.gable --emit-c   # shows the generated C17
```

`gable --help` lists every option. [docs/getting-started.md](docs/getting-started.md)
documents the build, the CLI, environment variables, and troubleshooting.

## Documentation

| Document | Contents |
| --- | --- |
| [docs/specification.md](docs/specification.md) | Language specification for version 0.1 |
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

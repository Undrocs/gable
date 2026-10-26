# Getting started

## Requirements

| Requirement | Version | Notes |
| --- | --- | --- |
| C++ compiler | C++20 | GCC 12+ or Clang 15+ |
| C compiler | C17 | GCC or Clang; used by the backend |
| CMake | 3.20+ | |
| Ninja | any | recommended; Make also works |
| Host | Linux or macOS | Windows is not supported yet |

No third-party libraries are required, and LLVM is not required to build the
compiler in this version (see [decisions.md](decisions.md), entry 0001).

## Build

```bash
git clone https://github.com/Undrocs/gable.git
cd gable

cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
```

Or with presets:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Other presets: `release`, `asan` (AddressSanitizer and UndefinedBehaviorSanitizer).

## Run the tests

```bash
ctest --test-dir build/debug --output-on-failure
```

Two CTest entries exist:

* `unit` — 142 fast unit tests (lexer, parser, semantics, code generation, CLI,
  robustness).
* `integration` — compiles and runs every case under `tests/integration/cases`
  and every program under `examples/`, comparing stdout, exit status, and
  diagnostics. It requires a working C compiler.

## Compile and run a program

```bash
cat > hello.gable <<'EOF'
fn main() -> i32 {
    print("Hello from Gable!");
    return 0;
}
EOF

./build/debug/gable build hello.gable -o hello
./hello

./build/debug/gable run hello.gable
```

`build` writes an executable named after the source file unless `-o` is given.
`run` compiles into a private temporary directory, executes the program, and
forwards its exit status.

Inspect the generated C (useful when reporting a backend bug):

```bash
./build/debug/gable build hello.gable --emit-c
```

## Command-line reference

```
gable --help | --version
gable build <file.gable> [options]
gable run <file.gable> [options]
```

| Option | Meaning |
| --- | --- |
| `-o <path>` | output path for `build` |
| `-O0` `-O1` `-O2` | optimisation level passed to the C backend (default `-O0`) |
| `--emit-c` | write the generated C to standard output instead of compiling |
| `--emit-c=<path>` | write the generated C to a file instead of compiling |
| `--keep-temps` | keep intermediate files and print their location |
| `-v`, `--verbose` | print the commands that are run |

Exit statuses:

| Status | Meaning |
| --- | --- |
| 0 | success |
| 1 | compilation failed (diagnostics on standard error) |
| 2 | usage error |
| 70 | internal failure: missing toolchain, or generated C did not compile |
| other | for `run`, the executed program's exit status |

A program terminated by a Gable runtime trap (integer division by zero) exits with
status 70.

## Environment variables

| Variable | Purpose |
| --- | --- |
| `GABLE_CC` | C compiler used by the backend (default `cc`) |
| `GABLE_RUNTIME_INCLUDE` | directory containing `gable/rt.h` |
| `GABLE_RUNTIME_LIB` | path to `libgable_rt.a` |
| `TMPDIR` | parent directory for temporary build directories |

The runtime locations are discovered automatically for a build tree and for an
installed compiler; the variables exist to override discovery.

## Install

```bash
cmake --install build/release --prefix ~/.local
```

This installs the `gable` binary, the runtime headers, `libgable_rt.a`, and the
documentation. The compiler library is not installed yet because its interface is
not stable. Add `~/.local/bin` to `PATH` and the installed compiler will find its
runtime through the install paths recorded at configure time.

## Learn the language

The supported subset is specified in [specification.md](specification.md).
`examples/` contains complete programs:

| Program | Demonstrates |
| --- | --- |
| `hello.gable` | functions, strings, output |
| `fibonacci.gable` | loops, `i64` arithmetic, helper functions |
| `fizzbuzz.gable` | conditionals, `else if`, integer remainder |
| `primes.gable` | nested loops, early returns |
| `collatz.gable` | mixed-width arithmetic, `while` |
| `gcd_lcm.gable` | mutable locals, division, function composition |

## Troubleshooting

**`cannot locate the Gable runtime headers`** — the compiler could not find
`gable/rt.h`. Set `GABLE_RUNTIME_INCLUDE` to the directory containing `gable/rt.h`
and `GABLE_RUNTIME_LIB` to `libgable_rt.a`.

**`internal error: the C compiler failed`** — the generated C did not compile.
The compiler keeps the generated file and prints its path; please attach it to a
bug report. This is a compiler defect, not a problem with your program.

**`gable: error: unsupported optimization level '-O3'`** — only `-O0`, `-O1`, and
`-O2` are accepted.

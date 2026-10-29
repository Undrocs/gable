# Getting started

## Requirements

| Requirement | Version | Notes |
| --- | --- | --- |
| C++ compiler | C++20 | GCC 12+ or Clang 15+ |
| C compiler | C17 | GCC or Clang on POSIX hosts, MSVC or clang-cl on Windows; used by the backend |
| CMake | 3.20+ | |
| Ninja | any | recommended; Make also works |
| Host | Linux or Windows | Exercised by CI with GCC and Clang on Linux and with MSVC on Windows. macOS and ARM hosts use the POSIX implementation but are not verified |

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

Where CMake and Ninja are not available, `tools/build-without-cmake.sh` performs
the same build with the host compilers and produces the same binaries:

```bash
tools/build-without-cmake.sh build/debug           # compiler and test binaries
tools/build-without-cmake.sh build/asan --sanitize # AddressSanitizer and UBSan
```

It prints the directory it built into. The CMake build is the supported path; the
script exists so that the compiler can be built on a machine without CMake.

## Run the tests

```bash
ctest --test-dir build/debug --output-on-failure
```

Six CTest entries exist:

* `unit` — 273 fast unit tests (lexer, parser, semantics including multi-file
  programs, code generation, CLI, diagnostics, robustness).
* `notes_tool` — builds `tools/notes` and drives it through its command line; this
  is the end-to-end test for a real multi-file program.
* `runtime_files` — calls the runtime's file builtins from C, which is the only
  way to reach a NUL byte in a path or a file above the read limit. POSIX only;
  it is not registered on Windows.
* `runtime_lists` — calls the runtime's list support from C: growth, shared
  handles, element layout, and reclamation.
* `runtime_memory` — checks that values reachable from the stack survive a
  collection and that unreachable ones are reclaimed.
* `integration` — compiles and runs every case under `tests/integration/cases`
  and every program under `examples/`, comparing stdout, exit status, and
  diagnostics. Example output is compared against `examples/expected/<name>.txt`,
  so an example is a test of the result it documents. Some cases pass program
  arguments. It requires a working C compiler.

Two further checks are scripts rather than CTest entries, and both need a POSIX
host:

```bash
tools/check-memory.sh build/debug/gable          # bounded memory for 1.6 GiB of short-lived strings
tools/measure-notes.sh build/debug/gable 20000   # timings of the notes tool on a large file
```

## Compile and run a program

```bash
cat > hello.gable <<'EOF'
fn main() {
    print("Hello from Gable!")
}
EOF

./build/debug/gable build hello.gable -o hello
./hello

./build/debug/gable run hello.gable
```

`build` writes an executable named after the source file unless `-o` is given.
`run` compiles into a private temporary directory, executes the program, and
forwards its exit status. `fn main()` exits with status 0; write
`fn main() -> int` and `return 1` to report a failure to the shell.

Inspect the generated C (useful when reporting a backend bug):

```bash
./build/debug/gable build hello.gable --emit-c
```

## Command-line reference

```
gable --help | --version
gable build <file.gable> [options]
gable run <file.gable> [options] [-- program arguments]
gable check <file.gable>
```

| Option | Meaning |
| --- | --- |
| `-o <path>` | output path for `build` |
| `-O0` `-O1` `-O2` | optimisation level passed to the C backend (default `-O0`) |
| `--emit-c` | write the generated C to standard output instead of compiling |
| `--emit-c=<path>` | write the generated C to a file instead of compiling |
| `--keep-temps` | keep intermediate files and print their location |
| `-v`, `--verbose` | print the commands that are run |

`check` runs the front end and writes nothing; use it in editors and pre-commit
hooks. Output paths are completed with `.exe` on Windows.

Program arguments follow `--` and are passed to the program `run` builds:

```bash
./build/debug/gable run tools/notes/notes.gable -- add "buy milk"
```

Exit statuses:

| Status | Meaning |
| --- | --- |
| 0 | success |
| 1 | compilation failed (diagnostics on standard error) |
| 2 | usage error |
| 70 | internal failure: missing toolchain, or generated C did not compile |
| other | for `run`, the executed program's exit status |

A program terminated by a Gable runtime trap (integer division by zero, or an
out-of-bounds array or list index) exits with status 70 and writes
`gable: runtime error: ...` to standard error.

## Environment variables

| Variable | Purpose |
| --- | --- |
| `GABLE_CC` | C compiler used by the backend (default `cc`, or `cl` on Windows) |
| `GABLE_CC_FLAGS` | extra options for that compiler, split on whitespace; use it to build generated programs with a sanitizer |
| `GABLE_RUNTIME_INCLUDE` | directory containing `gable/rt.h` |
| `GABLE_RUNTIME_LIB` | path to `libgable_rt.a` |
| `TMPDIR` | parent directory for temporary build directories on POSIX hosts (Windows uses `%TEMP%`) |

The runtime locations are discovered automatically for a build tree and for an
installed compiler; the variables exist to override discovery.

## Install

```bash
cmake --install build/release --prefix ~/.local
```

This installs the `gable` binary, the runtime headers, the runtime library
(`libgable_rt.a`, or `gable_rt.lib` on Windows), and the documentation. The
compiler library is not installed yet because its interface is not stable. Add
`~/.local/bin` to `PATH` and the installed compiler will find its runtime through
the install paths recorded at configure time.

## Learn the language

The language is specified in [specification.md](specification.md). This is a
complete program:

```gable
// Greet the people named on the command line, or one default person.
fn greeting(name: str) -> str {
    return "hello, $name"
}

fn main() {
    if arg_count() < 2 {
        print(greeting("world"))
        return
    }
    for index in 1..arg_count() {
        print(greeting(arg(index)))
    }
}
```

```
$ gable run greet.gable
hello, world
$ gable run greet.gable -- Ada Grace
hello, Ada
hello, Grace
```

Five things to notice, all of them the whole of the rule they belong to:

* A **statement ends at the end of the line**. `;` is accepted and ignored, so
  nothing breaks if you write it.
* **`int` is the number type**: 64-bit, wrapping on overflow, and the type of every
  length and index. `i8`–`u64` exist for data that has a width; `as` converts.
* **A string can contain its values**: `"hello, $name"`, `"${count + 1}"`,
  `"\$5"` for a literal dollar sign.
* **Operations are written on the value**: `text.trim()`, `text.split(",")`,
  `parts.join("-")`, `items.push(x)`. `.len` is a property, so no parentheses.
* **`fn main()`** runs and exits with status 0; **`fn main() -> int`** returns the
  exit status.

A failure is a value, and `else` handles it where the value is bound:

```gable
fn main() -> int {
    let read = fs_read_text("tasks.txt") else {
        eprint("cannot read tasks.txt: ${read.error}")
        return 1
    }
    print("${read.text.len} bytes")
    return 0
}
```

The `else` block must end the statement (`return`, `break`, or `continue`), so
after it the binding is the successful value and nothing has to re-check it.

`examples/` contains complete programs, each of which the test suite runs and
compares against its recorded output:

| Program | Demonstrates |
| --- | --- |
| `hello.gable` | the smallest program: a function, a string, output |
| `collatz.gable` | variables, `while`, arithmetic, conditions |
| `fizzbuzz.gable` | `if`/`else`, helper functions returning `bool`, building text |
| `fibonacci.gable` | functions, ranges, a loop that keeps state |
| `gcd_lcm.gable` | mutual helper calls, division and remainder |
| `primes.gable` | nested loops, early `return`, a growable list |
| `lists.gable` | `[T]`, `.push`, indexing, iteration, a list of structs |
| `strings.gable` | interpolation, `.len`, `.trim`/`.split`/`.join`, `.find`, `.slice`, `parse_i64` |
| `structs.gable` | structs as values, nested fields, returning a modified copy |
| `tasks.gable` | a fixed array of structs, formatting a numbered list |
| `args.gable` | `arg_count()` and `arg(i)` |
| `read_file.gable` | reading a file, `let ... else`, exit statuses |
| `records.gable` | turning text into structs and summarizing them |
| `calculator.gable` | recursive descent over tokens: precedence and parentheses |

Text that grows: a list collects pieces and `.join` puts them together once.

```gable
fn main() {
    let names: [str] = []
    for field in "ada|grace|alan".split("|") {
        names.push(field.trim())
    }
    print(names.len)                  // 3
    print(names[1])                   // grace
    print(names.join(", "))           // ada, grace, alan
}
```

Memory for strings and lists is reclaimed automatically; a program does not free
anything (specification §12). `tools/check-memory.sh` measures it: a program that
allocates 1.6 GiB in short-lived strings peaks at about 2.6 MiB.

`tools/notes` is a larger program split across three files; it accepts arguments,
keeps tasks in a text file, and has its own test suite. It saves through a
temporary file that replaces the notes file in one step, and takes `<file>.lock`
while it rewrites, so an interrupted save keeps the previous contents and two
commands do not lose each other's changes:

```bash
gable run tools/notes/notes.gable -- add "write the release notes"
gable run tools/notes/notes.gable -- list
```

## Troubleshooting

**`cannot locate the Gable runtime headers`** — the compiler could not find
`gable/rt.h`. Set `GABLE_RUNTIME_INCLUDE` to the directory containing `gable/rt.h`
and `GABLE_RUNTIME_LIB` to `libgable_rt.a`.

**`no 'main' function found`** — an executable needs `fn main()` or
`fn main() -> int` in the root file or in one of the imported files.

**`'main' must return 'int' (the exit status) or nothing at all, found 'i32'`** —
`main` has no parameters and returns either nothing or `int`.

**`internal error: the C compiler failed`** — the generated C did not compile.
The compiler keeps the generated file and prints its path; please attach it to a
bug report. This is a compiler defect, not a problem with your program.

**`gable: error: unsupported optimization level '-O3'`** — only `-O0`, `-O1`, and
`-O2` are accepted.

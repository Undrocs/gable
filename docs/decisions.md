# Design decisions

Short records of decisions that shaped the implementation, including the
alternatives that were rejected. Each entry states the decision, the reason, and
what would have to change for the decision to be revisited.

---

## 0001 — Generate C17 instead of LLVM IR for the first backend

**Context.** The language is intended to compile to native code through LLVM.
At the time of the v0.1 implementation no LLVM development package, headers, or
`clang` were available in the build environment, and no package manager could
install one. An LLVM-based backend could therefore have been written but not
built, tested, or demonstrated.

**Decision.** Keep LLVM as the target backend, but implement the first backend as
a C17 emitter compiled by the host C compiler. The typed IR is the boundary: a
second backend consumes the same `ir::Module`.

**Consequences.**

* The compiler produces real native executables today, and every code path is
  exercised by the test suite.
* Signed arithmetic is emitted in the unsigned domain so wrapping cannot become
  C undefined behaviour; division goes through runtime helpers so the specified
  trap behaviour is real.
* Programs inherit the platform C compiler's optimiser and code-generation
  quality. This does not delimit the language's semantics: the spec defines
  wrapping arithmetic and division traps, and the backend implements those
  definitions.
* Diagnostics for code generation are the C compiler's, translated into a
  "compiler defect" message with the generated C retained.

**Rejected alternatives.**

* *Write an LLVM backend untested.* Rejected: unverified code generation is worse
  than a verified simpler backend, and the project's rules forbid presenting it as
  working.
* *Direct x86-64 emission.* Rejected: an enormous amount of unverifiable work for
  a worse result than the C emitter, with no path to other targets.
* *Interpret the IR.* Rejected: it would not demonstrate native executable
  generation, which is the milestone.

**Revisit when.** LLVM development files are available in the build environment.
The work is: initialise targets and `LLVMContext`, lower each `ir::Stmt` to basic
blocks, use `alloca` for local slots, replace the division helpers with explicit
zero checks and `sdiv`/`srem`, keep the runtime for printing, then add a `--emit-llvm`
diagnostic mode and make it a backend selection option rather than a default.

---

## 0002 — A typed tree IR, not a control-flow graph

**Context.** A IR between semantic analysis and code generation pays off only if
it removes real complexity from the backend. A CFG with SSA construction is a
substantial subsystem.

**Decision.** `ir::Module` is a typed, structured tree: statements keep their
nesting, but all types, local slots, and callee indices are resolved.

**Consequences.** Backends can emit directly from structure (as the C backend
does) or build basic blocks during lowering (as an LLVM backend will). There is
no separate IR verifier; instead, semantic analysis guarantees the invariants and
the codegen tests assert the emitted shape.

**Revisit when.** The language gains `break`, `continue`, or any construct that
makes structured lowering awkward, or when a second backend needs a normalised
form for optimisations that the typed tree cannot express.

---

## 0003 — Semicolons are required; there is no automatic insertion

**Decision.** Statements end with `;`, and the parser reports a missing semicolon
at the point where one was expected. Newlines are whitespace.

**Why.** Automatic semicolon insertion interacts badly with a language that may
later grow expression-oriented syntax and multi-line expressions. An explicit
terminator keeps parsing decisions local and diagnostics pointing at the real
problem.

**Consequence.** Missing semicolons are a common beginner error; the diagnostic
names the construct (`expected ';' after variable declaration`) rather than just
the token.

---

## 0004 — No implicit numeric conversions; literals are context-typed

**Context.** Mixed-width arithmetic (`i32 + i64`) is a routine source of bugs in
C and C++. Conversion rules are also the hardest part of a type system to change
later without breaking programs.

**Decision.** Values never convert implicitly. Integer literals are the sole
exception: they have no type of their own and take the type demanded by the
context, with a range check. Unconstrained literals are `i32`.

**Consequences.** `let x: i64 = 1 + 2;` works, as does `takes_i64(42)`, but
`i32_value + i64_value` is an error with a message explaining that Gable does not
convert between integer types. Since v0.1 has no casts, mismatched values must be
kept in a single type by the programmer.

**Revisit when.** A cast or conversion operator is designed. That is the point at
which widening vs. narrowing rules must be decided, with tests.

---

## 0005 — Integer overflow wraps and is defined

**Decision.** Arithmetic on all integer types wraps modulo 2^n. Overflow is never
undefined behaviour, never traps, and is not a compile-time error.

**Why.** Unspecified overflow is a security hazard and makes programs depend on
optimiser behaviour. Trapping overflow is safer in some domains but makes
arithmetic expensive everywhere and complicates counters and hashes.

**Consequence.** The C backend must perform signed arithmetic through unsigned
types to avoid C's undefined behaviour; this is the reason the generated code is
full of casts. If checked arithmetic is added later, it should be an opt-in mode
or a new set of operators, not a change to `+`.

---

## 0006 — Division by zero traps at runtime with a defined exit status

**Decision.** Constant zero divisors are compile-time errors. A dynamic zero
divisor terminates the program with a message on standard error and exit status
70. `INT_MIN / -1` wraps to `INT_MIN`.

**Why.** Leaving division by zero undefined (as in C) leaks unspecified behaviour
into language semantics; returning a value silently would hide a bug.

**Consequence.** Division goes through runtime helpers, which costs a call at
`-O0`. `INT_MIN / -1` is defined as wrapping for consistency with 0005.

---

## 0007 — `print` is a compiler built-in, not a runtime function or a library

**Decision.** `print` is resolved by semantic analysis and lowered to the matching
`gable_rt_print_*` call. It is not a library function, has no first-class value,
and its name is reserved.

**Why.** v0.1 has no generics or traits, so an overloaded library `print` cannot
be expressed. A built-in keeps the runtime ABI narrow and avoids inventing
language features for the sake of one function.

**Revisit when.** A standard library with traits or generics exists; `print` should
then become a normal function in `stdlib`.

---

## 0008 — `str` is immutable, NUL-terminated UTF-8 with no operations

**Decision.** `str` values are immutable NUL-terminated byte sequences holding
UTF-8. v0.1 supports printing only. Escapes that would produce a NUL byte are
rejected at compile time.

**Why.** A slice (pointer plus length) would be a better long-term representation
but requires either a struct-like type or two values per string, both of which are
deferred. Rejecting NUL keeps the displayed value equal to the full value.

**Revisit when.** The language has aggregates or slices; `str` should then carry an
explicit length and support concatenation, comparison, and indexing.

---

## 0009 — Functions are declared, not hoisted with forward declarations

**Decision.** All functions in the file are visible to each other regardless of
order; users never write forward declarations. The compiler emits C forward
declarations.

**Consequence.** Mutual recursion works. Duplicate names are errors, and there is
no shadowing in the function namespace.

---

## 0010 — `main` must be `fn main() -> i32`

**Decision.** The entry point is fixed: no parameters, `i32` return, exactly one
`main`.

**Why.** Arguments and environment access require either arrays or platform
wrappers; both are out of scope. Fixing the signature makes the contract obvious
and the generated C trivial.

**Revisit when.** The standard library can expose arguments as a slice.

---

## 0011 — Bounded recursion in the frontend

**Decision.** Block and expression nesting is limited to 200 levels; exceeding the
limit is a diagnostic and parsing stops.

**Why.** Recursive descent plus recursive tree walkers use the process stack in
proportion to nesting. Without a limit, a 5,000-level nested block file crashed
the compiler during development (it exhausted memory through unbounded recovery).
Input is untrusted by definition.

**Consequence.** Legitimate programs are unaffected (200 levels of nesting is far
beyond readable code); the limit is documented and tested.

---

## 0012 — Diagnostics go to standard error; `run` forwards the program's status

**Decision.** Compiler diagnostics are written to standard error. `gable run`
forwards the program's exit status; compiler failures use 1 (compilation), 2
(usage), and 70 (internal). `gable run` therefore cannot distinguish a program
returning 1 from a compilation failure by exit status alone, only by output.

**Why.** Exit statuses are the interface scripts use; a fixed scheme is preferable
to inventing offsets. The overlap is documented rather than hidden.

**Revisit when.** The CLI grows machine-readable output (`--json`), which can carry
unambiguous outcomes.

---

## 0013 — Process execution without a shell

**Decision.** Every child process is started with an argument vector
(`fork` + `execvp`), never through `system` or a shell string. Temporary
directories come from `mkdtemp` with mode 0700 and are removed with RAII.

**Why.** Source paths are user-controlled and may contain spaces, quotes, or shell
metacharacters; shell interpolation is a command-injection hazard.

**Consequence.** The driver is POSIX-only for now, and Windows configuration fails
with an explicit explanation instead of a broken build.

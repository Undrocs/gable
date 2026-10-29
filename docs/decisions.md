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

## 0003 — Semicolons are required; there is no automatic insertion (superseded by 0023)

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

> **Superseded by ADR 0017.** v0.2 made `str` an explicit-length value with
> comparison, concatenation, and library operations.

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

## 0010 — `main` must be `fn main() -> i32` (superseded by 0025)

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

---

## 0014 — Evaluation order is specified, not inherited from C

**Decision.** Gable defines left-to-right evaluation of operands and call
arguments. The C backend enforces it: any subexpression with an observable effect
(call, allocation, trap, index check, aggregate construction) is stored in a
temporary before the next operand is evaluated. `&&` and `||` keep short-circuit
semantics, and the right operand is emitted inside an `if` when it has effects.

**Why.** C leaves the order of function arguments and most binary operands
unspecified, so a Gable program with two side-effecting calls could behave
differently on different hosts or optimization levels. Relying on the host compiler
to keep a chosen order is not a guarantee.

**Cost.** Generated C contains more temporaries than a hand-written translation
would. Temporaries are pure reads, so the C compiler still optimizes them away.

**Alternatives rejected.** (a) Documenting "unspecified, like C" — unacceptable for
a language that claims defined semantics. (b) Emitting statement expressions or
comma operators around every operand — the comma operator makes the code harder to
read and still needs sequencing statements for traps.

**Revisit when.** A backend emits LLVM IR: sequencing then becomes explicit in the
IR, and the temporaries disappear.

---

## 0015 — One program-wide namespace built from an explicit import graph

**Decision.** `import "path";` resolves relative to the importing file, each file
is parsed once, cycles are errors, and every declaration of every file shares one
namespace. Imports must precede other declarations.

**Why.** Multi-file programs need name resolution that is explained in one
paragraph and testable without infrastructure. A package manager, module names,
visibility rules, or per-file namespaces would each add concepts that v0.2 cannot
justify.

**Consequences.** Duplicate names are reported with both locations, independent of
file. Load order is deterministic: dependencies are analysed before the files that
import them. Two files cannot declare different things with the same name, so
splitting a program into files never changes its meaning.

**Alternatives rejected.** (a) Include-style textual insertion: loses source
locations and encourages re-parsing. (b) Per-file namespaces with `use`: needs
qualified names, aliasing, and a rule for ambiguity; that is a module system, not a
minimal import model.

---

## 0016 — Structs and arrays are values; arrays have a static length

**Decision.** Structs and fixed-size arrays are copied on assignment, argument
passing, and return. Array length is part of the type. There are no references,
pointers, or dynamic arrays.

**Why.** Value semantics remove aliasing questions (including "does iterating
observe a mutation?") from the language. Static lengths make layout and bounds
checking trivial and keep the C backend honest, since the generated type is a
wrapper struct around a C array.

**Consequences.** The notes tool in `tools/notes` reads the whole notes file into
one string and rewrites it, instead of holding a growable list of tasks. A struct
cannot contain itself by value.

**Alternatives rejected.** (a) Reference types with ownership rules: a large design
(the v0.3 memory work). (b) A built-in growable array: needs heap management and a
safety story that v0.2 does not have.

**Revisit when.** The memory model is designed; dynamic sequences belong there.

---

## 0017 — `str` is an immutable UTF-8 value with an explicit length (supersedes 0008)

**Decision.** A `str` is a pointer and a byte length. Literals are valid UTF-8,
NUL is rejected, `==` compares bytes, `+` allocates a concatenation, `.len` counts
bytes, and there is no indexing. Strings created at run time are never freed.

**Why.** Explicit lengths allow embedded text without a terminator and make the
cost of `==` and `.len` obvious. Concatenation without reclamation is a deliberate
v0.2 compromise: it is correct, simple, and documented, and the language is not
yet intended for long-running processes.

**Consequences.** Byte-oriented operations can split a UTF-8 character; the
specification says so rather than pretending otherwise. Programs that build strings
in loops grow their memory use.

**Alternatives rejected.** (a) NUL-terminated strings: cannot represent all byte
sequences and makes `len` O(n). (b) Reference counting now: ownership and cycle
questions belong with the memory model, not with the string type.

---

## 0018 — Recoverable failures return result structs instead of trapping

**Decision.** File input and output and number parsing return structs with an
`ok` field and an `error` or partial-value field. Only conditions the language
cannot express — division by zero, out-of-bounds indexing — trap.

**Why.** A compiler-inserted trap is not something a Gable program can react to;
a missing file is an expected situation for a command-line tool. Making the result
a struct keeps error handling in the language with no exceptions, no sum types, and
no new syntax.

**Cost.** Callers must inspect `.ok`; ignoring it silently keeps the default
values (`false`, empty text, zero).

**Alternatives rejected.** (a) Exceptions: unwinding across a C runtime boundary
and through a C backend is not worth it. (b) Panicking on I/O errors: unusable for
tools. (c) `Option`/`Result` generics: generics are deferred.

---

## 0019 — Outputs are written to a temporary file and moved into place

**Decision.** No output path is ever written directly. `gable build` and
`--emit-c=<path>` create a temporary file in the destination directory, write it,
and replace the destination in one step (`MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)`
on Windows, `rename` elsewhere). Before writing anything, the destination is
compared with every input file of the compilation, including through symlinks and
hard links.

**Why.** An interrupted or failing build must not destroy a working executable,
and a mistyped `-o` must not truncate the program being compiled. Both were
possible in v0.1.

**Consequences.** A failed build leaves the previous file untouched; a rejected
destination exits with status 2 and a message naming the input it aliases; no
temporary files are left behind on success or failure.

---

## 0020 — A native Windows implementation, not a POSIX compatibility layer

**Decision.** Windows support is implemented directly: a `platform/windows.cpp`
behind the platform seam (wide-character paths, `%TEMP%`, `MoveFileExW`) and a
`process_windows.cpp` that starts children with `CreateProcessW`. Arguments are
quoted with the Microsoft C runtime rules, captured output goes through temporary
files, and MSVC/clang-cl receive their own option syntax.

**Why.** A POSIX emulation layer would put an untested dependency between the
compiler and the platform, and WSL is a different operating system from the user's
point of view. Capturing output in files rather than pipes avoids the classic
two-pipe deadlock without threads.

**Consequences.** Two implementations of the platform seam must be kept in step.
The Windows path is verified by a CI job on GitHub's `windows-latest` with MSVC; no
other Windows toolchain is advertised.

**Alternatives rejected.** (a) MinGW-only support: it is not the default toolchain
users have. (b) Documenting WSL as the Windows story: it is not a native
implementation and leaves the compiler untested on the platform it claims.

---

## 0021 — Memory is reclaimed by a conservative mark-and-sweep collector

**Decision.** The runtime owns a heap for `str` values and for the buffers that
`fs_read_text` returns. A mark-and-sweep collector traces from the machine stack
and the callee-saved registers: a word is treated as a pointer when it falls
inside a block the runtime allocated. Blocks that are not reached are freed.
Collection runs during allocation, once the bytes allocated since the last cycle
exceed a threshold of `max(1 MiB, 2 × live bytes)`. The generated entry point calls
`gable_rt_init()` before anything else, which records where the thread's stack
begins and arranges for the heap to be released at exit.

**Why.** Programs needed to be able to run for longer than a handful of seconds
without growing without bound, and doing so must not cost the language its
simplicity: the memory model is what makes Gable teachable. A conservative
collector keeps every guarantee the language already made — value semantics,
immutability of `str`, no annotations — and adds reclamation underneath. The
alternatives all change what the programmer has to know:

* *Never reclaim* (the previous behaviour): correct, but memory grows with every
  concatenation, which rules out the command-line tools the language targets.
* *Reference counting*: needs a heap-allocated representation for every value that
  can alias, adds an ownership question at every copy, cannot collect cycles, and
  makes the cost of assignment visible in a language whose whole point is that
  assignment is cheap and predictable.
* *Ownership and borrowing*: a real answer, but it makes the first program a
  beginner writes require a lifetime discussion, and it is a much larger change
  than reclamation alone.
* *Precise tracing with per-type maps*: exact rather than conservative, and it
  would need type information for every stack slot, which the C backend cannot
  express without changing how the whole backend passes values.

Mark-and-sweep is also the only option here that keeps the existing C backend: the
generated code stores values in ordinary C variables and struct fields, and the
collector finds them by looking, so no generated code changed beyond one call.

**Consequences.** Peak memory is the live set plus pointer-shaped leftovers on the
stack, never less than the live set. A collection pauses the program while it
scans the live heap, the stack, and the block table; the table is kept sorted so
the scan is a binary search per word. The runtime must be single-threaded, which
the language already is; concurrency will require per-thread stacks and a
safepoint before it can be added. Heap memory is never freed by hand: the runtime
and its tests allocate through `gable_rt_allocate` and drop references. On a host
that cannot report the thread's stack bounds the collector stays off and memory is
not reclaimed, which is safe and documented (specification §12.3).

**Verification.** `runtime/tests/gc_tests.c` (15 checks) covers values surviving a
collection, unreachable blocks being reclaimed, automatic collection under
allocation pressure, interior pointers keeping their block alive, chains reachable
only through the heap, and 200 000 short-lived allocations. `tools/check-memory.sh`
runs a real compiled program that allocates 1.6 GiB and requires a peak under
16 MiB. Both were checked against deliberately broken runtimes: skipping the root
scan frees live values (the test crashes), and skipping collection grows the peak
to 3.2 GiB (the script fails).

**Rejected detail: deriving the stack top from a local variable.** The first
implementation had the generated `main` pass the address of one of its own locals.
That is not sound: the compiler may place locals above that address, and
AddressSanitizer does, which made a live array of pointers invisible to the
collector and a live block get freed. Each host is now asked for the thread's
stack bounds (`pthread_getattr_np` on Linux, `pthread_get_stackaddr_np` on macOS,
`GetCurrentThreadStackLimits` on Windows).

---

## 0022 — A list is a growable handle, not a value

**Decision.** `[T]` is a growable list of `T`; `[T; N]` stays a fixed-length
array. A list value is a handle to heap storage that holds the elements, their
count, and their capacity. Copying a list copies the handle, so every copy sees
the same elements, and `list_push(list, value)` is visible through every copy.
Elements are bounds checked, `.len` is the count, and `for item in list` iterates.

**Why.** Programs needed a collection that grows, and the task-list tool cannot be
written honestly without one. The hard question is what happens when a list is
assigned or passed. There were two coherent answers:

* *A list is a value*: assignment copies the elements. Then `list_push(items, x)`
  inside a function cannot be visible to the caller, which makes an append helper
  impossible without returning a new list and reassigning it at every call site.
  Copy-on-write would fix the visibility and make every call site pay a copy, and
  the copy cost would be invisible in the source — the opposite of what this
  language is for.
* *A list is a handle*: appends are visible, passing a list is O(1), and the cost
  of any line is visible where it is written.

The second was chosen. It is the model a reader already knows from JavaScript
arrays and Go slices, and it keeps Gable's rule that mutation appears in the
source: elements change only where `list[i] = v` or `list_push` is written.

**Consequences.** A `let` binding that holds a list does not freeze the elements;
the specification says so in §6.1 and §6.6 instead of leaving it to be discovered.
Two bindings can name the same list, so a function can change its caller's
collection — deliberately, as with any in-place append. `.len` is read from the
shared storage, so every handle agrees. A list that no handle names is reclaimed by
the collector (ADR 0021), including its storage. Element types do not need
generics: one implementation stores elements `stride` bytes apart and the compiler
knows the element type, which is enough for `list_push`, indexing, and iteration.

**Alternatives rejected.** (a) Value semantics with copy-on-write: invisible copies
and a reference-count-like model without the benefits. (b) Requiring `var` for
`list_push`: it would not actually prevent aliasing — a `[Task]` parameter can be
appended to — so it would teach a rule that is not true. (c) Generics plus a single
`List<T>`: the mechanism the directive defers until something needs it; a
monomorphic `[T]` with a runtime element size is smaller and does the same work.
(d) A separate `list_push` type per element type: more runtime surface for no
benefit.

**Measured.** `tools/notes` was rewritten on top of lists and `str_split`/
`str_join`; its behaviour is unchanged (all 83 of its tests pass) and it shrank
from 483 to 396 lines. Timing the same commands on a 20 000-task file with
`tools/measure-notes.sh` on one Linux host:

| Command | Line-at-a-time (`+` in a loop) | Lists and joins |
| --- | --- | --- |
| `list` | 4874 ms | 247 ms |
| `done 1` | 7712 ms | 220 ms |
| `remove 1` | — | 222 ms |

The old version copied the remaining file for every line it processed, so the
cost was quadratic in the number of tasks; the new one splits once, edits a list,
and joins once. The numbers are wall-clock milliseconds including process
start-up on that host, which is the same host for both builds.

**Verification.** `runtime/tests/list_tests.c` covers growth across doublings,
shared handles, element stride, and reclamation. The integration cases `lists`,
`list_bounds_trap`, and `error_list_element_type` run real programs, `examples/
lists.gable` is executed by the suite, and the unit tests cover parsing `[T]`,
element type checking, and the generated C for appends, indexing, assignment
through a list element, and iteration.

---

## 0023 — A statement ends at a line break; `;` is accepted (supersedes 0003)

**Decision.** A line break, `;`, `}`, or the end of the file ends a statement.
`;` is accepted and ignored, so both styles parse to the same program.

**Why.** The language is read far more often than it is written, and a
semicolon at the end of every line is punctuation that carries no information:
the line break already says where the statement ends. Beginners forget them, and
the resulting error ("expected ';'") is about the compiler's convenience rather
than their program.

ADR 0003 chose required semicolons to keep parsing decisions local. That concern
was answered rather than argued away: a line break ends a statement only where the
statement *could* end — an expression that is unfinished (`1 +` at the end of a
line) continues — and the parser's `endStatement` is the single place that decides
it, so the rule is one function, not a set of special cases spread through the
grammar.

**Consequences.** Existing programs with semicolons keep working unchanged, which
is why `;` was kept rather than removed. A program that wants two statements on one
line needs `;`, and forgetting it produces `expected a line break after a
declaration, found 'let'` with the note `a line break ends a statement; ';' is
also accepted`. Every example, the notes tool, and the specification are written
without semicolons so that the style a reader copies is the one the language is
designed around.

**Alternatives rejected.** (a) Keep semicolons required: works, but the punctuation
is noise and the commonest beginner error stays. (b) Remove `;` from the lexer: it
would break every existing program and every test case for no gain, and `;` is
unambiguous where a line break is not (two statements on one line).

**Verification.** `parser_tests` cover both spellings, `tests/integration/cases/
error_missing_semicolon` still reports the same mistake, and every example,
integration case, and the notes tool build and run without `;`.

---

## 0024 — `int` is the everyday integer; lengths and indices are `int`

**Decision.** `int` is a signed 64-bit integer and the type a program uses unless
it has a reason to name a width. `i64` is the same type under its width name; the
literals and the built-ins use `int`, and `typeName` prints `int` for it.

**Why.** A beginner's first question about `i32` vs `i64` has no answer that helps
them write a task list, and the old default made the language annoying in exactly
the way the simple cases matter: `.len` was `i64`, `arg(i)` took `i32`, a slice
bound was `i32`, so `text.slice(0, text.len)` needed a conversion, and every
example carried annotations it did not want. One type for counts, indices, and
numbers removes a whole class of error from ordinary code.

**Consequences.** The narrow widths remain for data that has a width (a value read
from a file, a mask, a byte), and `as` is still the only way between them. The
mismatch message now points at the fix: `Gable does not convert between number
types on its own; write an explicit conversion with 'as', for example 'value as
int'`. `print`, interpolation, and `str_of_i64` accept every integer width, so a
narrow value can be shown without converting it first.

**Alternatives rejected.** (a) Only `int`, no widths: the runtime already reads
and writes them and the notes tool uses `u8` for flags; removing them would remove
capability. (b) Make `int` 32-bit and keep `i64` for the rest: the same
conversion problem on a different line. (c) Keep every count `i64` and let
literals adapt: literal adaptation already worked, but `.len` still came back as a
different type whenever it was stored.

**Verification.** `semantic_tests` and `codegen_tests` cover `int` in each
position; `tests/integration/cases/types`, `casts`, and `error_mixed_integer_types`
exercise the rules; `examples/` uses `int` throughout, with `i64` only where a
width is the point.

---

## 0025 — `main` needs no return type (supersedes 0010)

**Decision.** `fn main()` is a complete program that exits with status 0;
`fn main() -> int` returns the process exit status. No other signature is accepted.

**Why.** A first program that must write `-> i32` and `return 0` before it can
print a line teaches the reader two ideas (`main` is a function, a program has an
exit status) as one ceremony. Most programs never look at their exit status and
already have a natural `void` shape.

**Consequences.** The two shapes both appear in the documentation, and the choice
is stated where it matters: a program that can fail (the notes tool, the file
example) declares `-> int` and returns 1 or 2 on failure, because a shell needs to
know. The return-path analysis is unchanged; a `-> int` main must return on every
path, including after an `if` that does not end in a `return`.

**Alternatives rejected.** (a) Keep `-> i32` required: it is one more thing to
explain before anything runs. (b) Allow `-> void` as a third spelling: `-> void`
already means the same as omitting the type, so a third spelling adds nothing.
(c) Default the exit status to the last expression: Gable has no block-valued
expression, and a hidden exit status is exactly the kind of invisible rule this
language avoids.

**Verification.** `tests/integration/cases/exit_code` runs a `-> int` program and
checks the status; `error_bad_main_signature` covers the rejected shapes; all 14
examples run through the suite, and 12 of them use bare `fn main()`.

---

## 0026 — A string literal may contain its values

**Decision.** `"hello $name"` and `"total: ${count + 1}"` build a string from its
parts. `$name` takes a variable, parameter, or loop variable; `${ ... }` takes any
expression; `\$` is a literal dollar sign; a `$` that starts neither is an
ordinary character.

**Why.** Joining text is one of the few things every program does, and
`"hello, " + name + "!"` is a puzzle for a beginner: it hides the shape of the
output in quotes and plus signs, puts spaces and punctuation at the edges of
literals, and reads backwards as it grows. Writing the output as it should look is
the difference between code that is read and code that is decoded.

**Consequences.** Interpolation is defined as concatenation, not as a new kind of
value: it allocates the same way `+` does, and the parts convert the way `print`
converts them (`str`, `bool`, any integer). A struct, array, or list inside an
interpolation is an error with the same explanation as `print` gives, which keeps
one rule for "what can be shown". Nested interpolation is bounded at 8 levels and
reported rather than allowed to consume stack. Because `$` was not previously
special outside a literal, no existing program changes meaning; inside a literal,
`\$` is the one escape a program may need to add, and `"$5"` still means `$5`.

**Alternatives rejected.** (a) `"hello, {name}"` or `"hello, $"{name}`: more
punctuation for the same idea, and a bare `{` inside ordinary text (JSON, for
example) would need escaping. (b) `format("...", args)`: a function call with a
format language inside a string, which is the thing being replaced. (c) No
interpolation, `+` only: it works, and the examples showed how it reads.

**Verification.** `lexer_tests` and `parser_tests` cover the three forms, the
escape, and the nesting limit; `tests/integration/cases/interpolation` prints a
matrix of values; `examples/` and `tools/notes` write every user-visible message
this way.

---

## 0027 — Receiver calls: the value comes first

**Decision.** The built-ins that operate on a value have a method spelling:
`text.trim()`, `text.split(",")`, `text.find(needle)`, `text.slice(start, length)`,
`parts.join(",")`, and `list.push(value)`. The free functions remain, and both
spellings mean exactly the same call.

**Why.** `str_trim(str_split(str_trim(line), ",")[0])` reads inside out, and the
argument a reader cares about most — the thing being worked on — appears last.
`line.trim().split(",")` reads left to right in the order the operations happen,
and matches how a reader already says it. `.len` was already a property read
without parentheses, so `text.len` next to `text.trim()` is consistent, and the
compiler can say `'len' is a value, not a method; write '.len' without '()'`.

**Consequences.** Methods exist only on `str` and lists, so there is no user-defined
method, no inheritance, and no ambiguity about what a call means: the receiver's
type selects the one built-in. Because the free function is still there, an error
message can show either spelling, and existing programs do not change. A struct
field called like a method is diagnosed as such (`'x' is a field of struct 'Point',
not a method; read it without '()'`), which is the other half of the same rule.

**Alternatives rejected.** (a) Methods only: it would break existing programs and
remove the form that reads better when the value is a literal (`parse_i64(text)`).
(b) A general method mechanism (`fn (str) trim`): user-defined methods need
receiver syntax, ambiguity rules, and a story for structs; none of that is needed
to make text operations readable. (c) Renaming the built-ins to `trim(text)`: the
same call, the same reading problem.

**Verification.** `semantic_tests` check that each method lowers to its built-in
with the receiver as the first argument and that the argument types are checked;
`diagnostic_tests` cover `.len()`, a field called as a method, and an unknown
method; `tests/integration/cases/methods` runs both spellings and compares output.

---

## 0028 — `let x = value else { ... }` handles a failure where it is bound

**Decision.** A binding whose initializer can report failure may handle the failure
in place. The initializer's type must have a `bool` field named `ok`; the `else`
block runs when `ok` is false, with the failed value in scope under the binding's
name, and must end the statement with `return`, `break`, or `continue`.

**Why.** Result structs (ADR 0018) are the right model — failures are values, not
exceptions — but using one costs three lines and a temporary that shadows the
value:

```gable
let read = fs_read_text(path)
if !read.ok {
    eprint("cannot read: " + read.error)
    return 1
}
print(read.text)
```

The same code with `else` reads as one idea: bind the value, and if there is no
value, do this instead. The binding that survives is the successful one, so the
rest of the function never has to re-check it.

**Consequences.** The check is structural (`ok: bool`), not a built-in type list, so
a program's own result struct works and the language gains no new type concept.
Requiring the block to leave the statement is what makes the binding safe: there is
no path on which `read` holds a failure. The three failures have their own
messages — no `ok` field, an `ok` that is not `bool`, and an `else` block that can
fall through — so the fix is stated in the error. `let` stays immutable; a failure
that is handled and then ignored is written with `if`, not with `else`.

**Alternatives rejected.** (a) Exceptions or `try`: invisible control flow, and
the runtime and every call would have to carry it. (b) `?` that returns the failure
from the enclosing function: it only fits functions with the same result type, so
it cannot be used in `main` or in a helper that returns a string. (c) A `match`
construct: pattern matching is deliberately not in the language yet, and a
single-case form would be a special case of a feature that does not exist. (d)
Requiring `if` after the binding: that is the code above, which is what the feature
is for.

**Verification.** `tests/integration/cases/file_guard` reads a present file, a
missing file, and a file holding text that is not a number, checking all three
messages and exit statuses; `semantic_tests` cover the three error shapes and the
`break`/`continue` forms; `tools/notes` uses the feature for every file operation,
and its 83 tests run against it.

---

## 0029 — The formatter changes whitespace and nothing else

**Decision.** `gable fmt` rewrites indentation, the spaces between tokens on a
line, trailing whitespace, and runs of blank lines. It never moves a token to
another line, never adds or removes a token, and never re-wraps an expression,
a call, or a list. Comments are preserved: a line comment stays on its line, and a
block comment keeps its own text, including the line breaks inside it. A file that
does not parse is reported and left alone. `--check` writes nothing and reports
through the exit status.

**Why.** In Gable a line break ends a statement (ADR 0023). A formatter that
re-wrapped code would have to decide where a statement ends, and any disagreement
between it and the parser changes the meaning of the program it was asked to
format. Restricting the formatter to whitespace *within* lines removes that risk
entirely: the token sequence is the same before and after, and the file's meaning
is a function of that sequence and its line breaks, both of which are preserved.

The second reason is that most of what a re-wrapping formatter is for — fitting an
expression onto one line, choosing where to break a long call — is a preference,
and a preference that churns diffs. Gable's formatting question is narrower: is
there one obvious indentation and one obvious spacing? There is, and that is what
the command enforces.

**Consequences.** A long line stays long; the formatter never shortens it. The
programmer decides where lines break, which is also where statements end, so the
file remains readable in the order it was written. Formatting is idempotent, which
`--check` depends on and which the unit tests assert for every rule. The formatter
works on a line at a time and keeps a brace/parenthesis/bracket depth, so it is
about 300 lines and has no dependency on the parser beyond "the file must parse
first" — the CLI runs the front end before it writes, and refuses on any error, so
a half-typed file is never rewritten.

**Alternatives rejected.** (a) Format from the AST and re-print the tree: comments
are not in the AST, so a formatter built from it would silently delete every
comment in the file. (b) Token-stream formatting that also decides line breaks:
the statement rule makes that a semantic decision, and it would be a second
implementation of the parser's line-break rule. (c) Re-styling with an external
tool: a dependency and a foreign policy for the one thing this language can define
for itself. (d) No formatter at all: the style in the examples and documentation
then has to be maintained by hand, which is where the inconsistencies in the 0.2
examples came from.

**Verification.** `tests/unit/format_tests.cpp` covers indentation, alignment of a
closing brace, blank-line folding, operator spacing, prefix versus binary `-`,
calls and indexes, multi-line lists, line and block comments, strings (including an
interpolation), idempotence, and both unterminated-input errors.
`tests/unit/cli_tests.cpp` covers `--check` (writes nothing, exit status 1), a
rewrite that lands through the atomic path, and a file with a syntax error that is
left byte-for-byte unchanged. The integration suite runs `gable fmt --check` on
every example, so all of them are formatted and stay that way.

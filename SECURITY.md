# Security policy

## Supported versions

Gable is pre-1.0 and has no released versions. The only supported code is the
current state of the default branch; fixes are made there and are not backported.

## Reporting a vulnerability

Do not open a public issue for a security problem.

Report it privately through GitHub: go to the **Security** tab of
`Undrocs/gable` and choose **Report a vulnerability**. Include:

* the Gable version (`gable --version`), host platform, and how the compiler was
  built;
* the input that triggers the problem (attach the `.gable` file, or a hex dump if
  it is not valid text);
* what happens (crash, hang, wrong executable, arbitrary file access, etc.) and
  what you expected;
* whether the issue is in the compiler or in the generated program.

You can expect an acknowledgement within about a week. This is a volunteer
project; there is no formal service-level agreement.

## What counts as a security issue

The compiler treats source files as untrusted input. Anything in this list is in
scope:

* memory-safety violations in the compiler (out-of-bounds access, use-after-free,
  uninitialised reads, double free);
* undefined behaviour in compiler internals reachable from source input;
* crashes or unbounded resource use triggered by malformed input (including
  stack exhaustion and memory exhaustion);
* compiler output that does not correspond to the program's specified semantics
  (silent miscompilation);
* unsafe temporary-file or output-path handling;
* command injection through paths, arguments, or environment variables;
* a produced executable behaving unsafely in a way the language specification
  promises it will not (for example, division by zero not trapping when it must).

Out of scope:

* the language allowing programs that misbehave by design (for example, a
  division by zero in a program that ignores the documented trap);
* vulnerabilities in a C compiler, linker, or system library used by the host;
* missing features and unsupported platforms;
* denial of service caused by extremely large but otherwise valid inputs beyond
  the documented limits, unless it bypasses a documented limit.

## How the compiler limits exposure

* Source size is capped (64 MiB) and nesting depth is capped (200 levels).
* Child processes are always started with argument vectors (no shell), so paths
  cannot be interpreted as shell syntax.
* Temporary files live in a private directory created with `mkdtemp` (mode 0700)
  and are removed on success; generated files are retained after an internal
  failure only so they can be inspected.
* Ordinary compilation never executes the program being compiled. Only
  `gable run` executes code, and it says so in `--help`.

## Disclosure

Reports are investigated, fixed on the default branch with a regression test, and
credited in the release notes (or the commit message while the project has no
releases) unless the reporter prefers to remain anonymous. Public disclosure is
coordinated with the reporter after a fix is available.

# Contributing to Gable

Gable is experimental and its language surface is deliberately small, but the
engineering standards are not. Contributions are welcome from anyone who is
willing to work within them.

## Before you start

* For anything larger than a typo or a small bug fix, open an issue first. It is
  cheaper to agree on a direction than to review a large unrequested change.
* Language changes (new syntax, new semantics, changed diagnostics for valid
  programs) need a proposal issue that answers: what problem it solves, what the
  alternatives are, and how it affects existing programs. Once a direction is
  agreed, the change lands with a specification update and tests in the same pull
  request.
* Read [docs/specification.md](docs/specification.md) and
  [docs/decisions.md](docs/decisions.md) first. If you believe a decision is wrong,
  argue against it in an issue with evidence rather than working around it.

## Building and testing

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

Sanitizers catch most memory and undefined-behaviour mistakes; run them for
changes to the lexer, parser, semantics, or IR:

```bash
cmake --preset asan
cmake --build --preset asan
ctest --test-dir build/asan --output-on-failure
```

The integration suite invokes a real C compiler, so a working `cc` is required for
any change that touches code generation or the driver.

## What a change is expected to include

* **Tests.** Every implemented behaviour needs a test. Compiler bugs need a
  regression test that fails before the fix.
* **No silent behaviour changes.** If valid programs compile differently, the
  specification and the tests that encode the old behaviour change together.
* **Warning-free builds.** The project builds with `-Wall -Wextra -Wpedantic
  -Wshadow -Wconversion -Wsign-conversion`. Do not add broad warning
  suppressions; fix the code or narrow the suppression at the exact site with a
  comment explaining why.
* **Generated-code cleanliness.** If you change the C backend, the generated code
  must still compile without warnings under `-Wall -Wextra`; the integration suite
  checks this.
* **Documentation.** Update `docs/` when behaviour, the CLI, or the architecture
  changes. Do not describe unimplemented features as working.
* **No new dependencies** without a justification in the pull request: what it
  provides, why writing it locally is worse, its licence, and its versioning
  policy.

## Style

* C++20, standard library only; no compiler-specific extensions without a guard.
* Follow the surrounding code: 2-space indentation, 100-column lines, `snake_case`
  for variables, `CamelCase` for types, `kConstantName` for constants.
* Prefer clear names and direct code over abstraction. Do not add a wrapper class
  for one function, an interface with one implementation, or a template that is
  instantiated once.
* Ownership is explicit: `std::unique_ptr` for owned heap objects, references for
  non-owning access, values for small types.
* Comments explain *why*, not *what*. Delete a comment if the code says the same
  thing.
* Formatting configuration lives in `.clang-format`; run `clang-format` on files
  you touch if you have it.

## Process

* Keep pull requests focused. One logical change per pull request.
* Commit messages: a short imperative subject line ("Reject duplicate parameter
  names"), then a body explaining the reasoning when it is not obvious.
* Explain in the pull request: what changed, why, how it was tested, and anything
  you are unsure about. A tested, small change with an honest description is
  easier to merge than a large, confident one.
* Review looks for correctness, specification conformance, test coverage,
  clarity, maintainability, security, and portability. Expect questions about
  failure paths and malformed input.
* Do not include generated artefacts (build output, binaries, temporary files).
  `.gitignore` covers the usual cases.

## Licensing

Gable is licensed under Apache-2.0. By submitting a contribution you agree that it
is your own work and that it is licensed under the same terms. No contributor
licence agreement is required. Do not paste code from projects with incompatible
licences.

## Conduct

Participation is governed by [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

## Security

Do not report security issues in public. Follow [SECURITY.md](SECURITY.md).

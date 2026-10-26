# GitHub language recognition and naming

This page records the state of two related external matters: GitHub Linguist
recognition for `.gable`, and the naming situation for "Gable".

## Linguist status

**Gable is not currently recognised by GitHub Linguist.** No pull request has been
opened, and none should be opened until the preconditions below hold. Linguist
treats unknown extensions as plain text, so `.gable` files are excluded from the
repository language statistics entirely: they do not appear as "Gable", and they
are not counted as "Other" either.

## Repository language statistics

GitHub's Languages section is produced by Linguist from the **default branch**.
`.gitattributes` at the repository root records the cases that matter here:

* `*.gable linguist-detectable=false` — Gable is not a Linguist language, so its
  sources are excluded from the statistics by design rather than by accident.
* `* text=auto eol=lf` — line endings are normalised, so re-cloning on another
  platform cannot change a file's byte count.

As of this commit the default branch contains 24 `.cpp`, 20 `.h`, and 2 `.c`
files, so the reported composition is C++ (about 96%), CMake (about 3%), and C
(about 1.5%). The 40 `.gable` files — examples and test cases — are not counted.

Practical consequences:

* Statistics are recomputed asynchronously after a push to the default branch.
  A repository that had no detectable code before (for example when it only
  contained `README.md` and `LICENSE`, both of which are not detectable) will show
  no Languages section until that recomputation finishes, and the page has to be
  reloaded.
* The section is only rendered for users who can read the repository. In a private
  repository that means users with access; an anonymous visitor sees a 404 and
  therefore no statistics at all.
* Gable itself cannot appear in the section until Linguist accepts the language.
  Mapping `.gable` onto an existing language with `linguist-language=` would put a
  false label on the project and is deliberately not done.

### Preconditions from Linguist's contribution guidelines

Research as of October 2026 (see the Linguist `CONTRIBUTING.md` and the
`languages.yml` documentation):

1. **Sufficient usage.** A new language is only accepted with wide-spread public
   usage — commonly cited as roughly 200 unique repositories — assessed through
   GitHub code search. New or hobby languages are closed without review, and
   "Hello world"-style samples are not accepted.
2. **A syntax-highlighting grammar.** A TextMate/VS Code compatible grammar under
   a compatible licence, vendored as a submodule, with `grammars.yml` wiring and a
   licence dependency entry.
3. **Representative samples** under `samples/`, of real-world code rather than
   tutorial snippets.
4. **Tests** in Linguist verifying language identification for the extension.
5. **A `languages.yml` entry** with `type`, `tm_scope`, and extensions; the
   `language_id` is assigned by GitHub, not by the contributor.

### Metadata Gable intends to propose

```yaml
Gable:
  type: programming
  color: "#83A48C"
  extensions:
    - ".gable"
  tm_scope: source.gable
```

This is a proposal, not an accepted registration. The sage colour `#83A48C` is used
consistently in Gable's own material in the meantime.

### Work that precedes a submission

* A Tree-sitter grammar and a TextMate grammar aligned with
  [specification.md](specification.md) — created only when the syntax is stable
  enough that the grammar does not advertise unsupported constructs.
* Real Gable programs from independent users.
* A dedicated repository for the grammar so it can be vendored as a submodule.

## Naming

`Gable` is used by several unrelated software products. Documented examples found
during a search of public sources:

| Project | Domain | Relation to a programming language |
| --- | --- | --- |
| Gable.ai (GitHub organisation `gabledata`) | data contracts / data platform | unrelated product name |
| `sandialabs/GABLE` (archived) | research prototype, Solidity contracts | unrelated |
| `gable-finance` | DeFi liquidity protocol repository | unrelated |
| `JustinMarbutt/GABLE` (archived) | academic L-grammar tool | unrelated |
| Personal accounts and surnames such as `jgable`, `mgable` | — | unrelated |

Practical implications:

* No evidence was found of an existing *programming language* named Gable, so the
  language name is not knowingly conflicting in its own domain.
* The `gable` command name does not collide with any standard system tool on Linux
  or macOS as far as this project can determine from a fresh installation.
* Package registry names, domain names, and trademarks have **not** been
  registered, checked against trademark databases, or reserved by this project. Any
  distribution beyond the source repository should verify those separately.
* The project name will not be changed silently. If a genuine conflict forces a
  rename, it will be discussed publicly in the repository first.

The repository itself is `Undrocs/gable`; the language's identity is defined by
Undrocs and by the specification in this repository, not by any of the unrelated
products above.

## Branding assets

A logo was designed for Gable before this repository's compiler work began. No logo
asset is present in the repository, so none is used here. The compiler and
documentation do not substitute an invented mark; the name, the sage colour
`#83A48C`, and restrained typography carry the identity until the original asset
is added by its owner. The logo must not be recreated or replaced.

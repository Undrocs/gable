# notes

A small task list written in Gable. It is the acceptance test for the 0.2
language work: a real multi-file command-line program that takes arguments, uses
strings and structured data, and keeps its tasks in a plain text file.

```
notes [--file <path>] add <title...>
notes [--file <path>] list
notes [--file <path>] done <number>
notes [--file <path>] remove <number>
notes [--file <path>] clear
```

The file defaults to `notes.txt` in the current directory. Task numbers start at
1.

## Build and run

```bash
gable build tools/notes/notes.gable -o notes
./notes add "write the release notes"
./notes add buy milk
./notes list
./notes done 2
./notes list
```

## Format

One task per line: a status flag (`0` pending, `1` done), a tab, and the title.
Titles may not contain tabs or line breaks, which keeps the format unambiguous
without an escaping mechanism. A file that does not follow the format is reported
and left untouched.

## Exit status

| Status | Meaning |
| --- | --- |
| 0 | success |
| 1 | usage error: unknown command, missing or invalid task number, invalid title |
| 2 | storage error: the file cannot be read, parsed, or written |
| 3 | the requested task number does not exist |

## Design notes

* Three files: `text.gable` splits lines and stored tasks, `storage.gable` reads,
  rewrites, and validates the file, `notes.gable` parses the command line and
  dispatches.
* Gable 0.2 arrays have a compile-time length, so the tool never holds the whole
  list in memory. Each command streams over the file contents and rewrites the
  parts it keeps.
* Reading a missing file yields an empty list rather than an error, so the first
  `add` creates the file.

## Tests

`tools/notes/tests/notes_tool_tests.cpp` builds the tool with the compiler under
test and drives it through its command line: adding, listing, completing,
removing, clearing, rejecting malformed input, refusing to overwrite a corrupt
file, and reporting unusable paths. It runs as the `notes_tool` CTest entry.

```bash
ctest --test-dir build/debug -R notes_tool --output-on-failure
```

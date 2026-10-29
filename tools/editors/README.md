# Editor syntax support

`gable.tmLanguage.json` is a TextMate grammar for `.gable` files. TextMate
grammars are what most editors use for syntax highlighting: VS Code, Sublime
Text, TextMate, and IntelliJ-based editors all accept this format, either
directly or through a small wrapper.

It highlights comments, string literals (including `$name` and `${...}`
interpolation and the escape sequences the language defines), integer literals
in decimal, hexadecimal, and binary, keywords, type names, the built-in
functions and the method spelling of the ones that operate on a string or a
list, struct and function names, operators, and punctuation.

## Using it

* **VS Code**: copy this directory into
  `~/.vscode/extensions/gable-0.3.0/` and add a `package.json` that contributes
  the grammar under the `source.gable` scope.
* **Sublime Text**: copy the grammar into `Packages/User/` and add a
  `.sublime-syntax` wrapper, or paste it into
  `Packages/User/Gable.sublime-syntax`'s `contexts`.
* **Any editor**: the file is JSON, so it can be loaded from a settings entry
  that takes a grammar path.

## Status

This grammar is not covered by the test suite: asserting that an editor
highlights code correctly needs an editor. What is checked is that the file is
valid JSON and that every keyword, type name, and built-in the compiler knows
appears in it, which `tests/unit/editor_grammar_tests.cpp` does by reading the
grammar and the compiler's own keyword and builtin lists.

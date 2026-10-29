// Tests for the TextMate grammar in tools/editors.
//
// An editor grammar cannot be tested without an editor, so what is checked here is
// the part that can be: the file is well-formed, it declares the scope the file
// type maps to, and every keyword, type name, and built-in the compiler knows
// appears in it. A name that is missing from the grammar is a highlighting gap
// that a reader would notice before a maintainer would.
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "test_framework.h"

namespace {

std::string readGrammar() {
  const std::string path = std::string(GABLE_SOURCE_DIR) + "/tools/editors/gable.tmLanguage.json";
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw gabletest::Failure("cannot read " + path);
  }
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

/// Counts `character` outside of string literals, which is enough to check that
/// the braces and brackets of the JSON nest.
bool bracketsAreBalanced(const std::string& text) {
  std::vector<char> open;
  bool inString = false;
  bool escaped = false;
  for (const char c : text) {
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        inString = false;
      }
      continue;
    }
    if (c == '"') {
      inString = true;
      continue;
    }
    if (c == '{' || c == '[') {
      open.push_back(c);
      continue;
    }
    if (c == '}' || c == ']') {
      if (open.empty()) {
        return false;
      }
      const char expected = c == '}' ? '{' : '[';
      if (open.back() != expected) {
        return false;
      }
      open.pop_back();
    }
  }
  return !inString && open.empty();
}

}  // namespace

GABLE_TEST(editor_grammar_is_structurally_sound) {
  const std::string grammar = readGrammar();
  GABLE_CHECK(!grammar.empty());
  GABLE_CHECK(bracketsAreBalanced(grammar));
  GABLE_CHECK(grammar.find("\"scopeName\": \"source.gable\"") != std::string::npos);
  GABLE_CHECK(grammar.find("\"fileTypes\": [\"gable\"]") != std::string::npos);
}

GABLE_TEST(editor_grammar_covers_every_keyword_and_type) {
  const std::string grammar = readGrammar();
  const std::vector<std::string> names = {
      // keywords
      "fn", "let", "var", "if", "else", "while", "for", "in", "return", "break",
      "continue", "struct", "import", "as", "true", "false",
      // type names
      "int", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "bool", "str",
      "void",
  };
  for (const std::string& name : names) {
    GABLE_CHECK(grammar.find(name) != std::string::npos);
  }
}

GABLE_TEST(editor_grammar_covers_every_builtin) {
  const std::string grammar = readGrammar();
  const std::vector<std::string> builtins = {
      "print",           "eprint",    "arg_count", "arg",        "fs_read_text",
      "fs_write_text",   "fs_write_text_atomic",    "fs_lock",    "fs_unlock",
      "str_of_i64",      "str_of_bool",   "parse_i64", "str_find", "str_slice",
      "str_trim",        "str_split",     "str_join",  "list_push",
      // method spellings and the length property
      "trim",            "split",         "join",      "slice",    "push",
      "len",
  };
  for (const std::string& name : builtins) {
    GABLE_CHECK(grammar.find(name) != std::string::npos);
  }
}

GABLE_TEST(editor_grammar_highlights_interpolation) {
  const std::string grammar = readGrammar();
  // Both forms from the specification: `$name` and `${expression}`.
  GABLE_CHECK(grammar.find("variable.other.interpolation.gable") != std::string::npos);
  GABLE_CHECK(grammar.find("meta.interpolation.expression.gable") != std::string::npos);
}

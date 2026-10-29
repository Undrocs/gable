// Robustness tests: the compiler frontend must never crash, hang, or corrupt
// memory on malformed input. Everything here is deterministic.
#include "test_framework.h"
#include "test_helpers.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

namespace {

/// Small deterministic pseudo-random generator, so failures are reproducible.
class Random {
 public:
  explicit Random(std::uint64_t seed) : state_(seed) {}

  std::uint32_t next() {
    state_ = state_ * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<std::uint32_t>(state_ >> 33);
  }

  std::size_t below(std::size_t bound) { return bound == 0 ? 0 : next() % bound; }

 private:
  std::uint64_t state_;
};

/// Feeds `text` through the whole frontend; a well-behaved compiler returns.
void analyzeWithoutCrashing(const std::string& text) {
  const gabletest::Analysis analysis = gabletest::analyze(text);
  // Rendering diagnostics must also be safe.
  GABLE_CHECK(!analysis.rendered.empty() || analysis.ok());
}

}  // namespace

GABLE_TEST(robustness_accepts_empty_and_whitespace_only_input) {
  analyzeWithoutCrashing("");
  analyzeWithoutCrashing("\n\n\t  \n");
  analyzeWithoutCrashing("// only a comment\n");
  analyzeWithoutCrashing("/* only a block comment */");
}

GABLE_TEST(robustness_reports_missing_main_for_empty_program) {
  const gabletest::Analysis analysis = gabletest::analyze("");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(gabletest::mentions(analysis, "no 'main' function found"));
}

GABLE_TEST(robustness_handles_random_token_soup) {
  const std::string alphabet =
      "fn let var return if else while true false i32 i64 bool str (){};,:+-*/%<>=!&|\"\\ 0123456789abxyz\n\t";
  Random random(0x5eed);
  for (int iteration = 0; iteration < 300; ++iteration) {
    std::string source;
    const std::size_t length = random.below(200);
    for (std::size_t i = 0; i < length; ++i) {
      source.push_back(alphabet[random.below(alphabet.size())]);
    }
    analyzeWithoutCrashing(source);
  }
}

GABLE_TEST(robustness_handles_random_bytes) {
  Random random(0xc0ffee);
  for (int iteration = 0; iteration < 200; ++iteration) {
    std::string source;
    const std::size_t length = random.below(120);
    for (std::size_t i = 0; i < length; ++i) {
      source.push_back(static_cast<char>(random.below(256)));
    }
    analyzeWithoutCrashing(source);
  }
}

GABLE_TEST(robustness_survives_mutations_of_a_valid_program) {
  const std::string baseline = gabletest::kSimpleProgram();
  Random random(0xbeef);
  for (int iteration = 0; iteration < 200; ++iteration) {
    std::string source = baseline;
    const std::size_t mutations = 1 + random.below(4);
    for (std::size_t i = 0; i < mutations; ++i) {
      const std::size_t position = random.below(source.size());
      switch (random.below(3)) {
        case 0:
          source[position] = static_cast<char>(' ' + random.below(95));
          break;
        case 1:
          source.erase(position, 1);
          break;
        default:
          source.insert(position, 1, static_cast<char>(' ' + random.below(95)));
          break;
      }
      if (source.empty()) {
        source.clear();
        source.push_back('x');
      }
    }
    analyzeWithoutCrashing(source);
  }
}

GABLE_TEST(robustness_rejects_excessive_expression_nesting) {
  std::string source = "fn main() { let a: i32 = ";
  const int depth = 5000;
  for (int i = 0; i < depth; ++i) {
    source += "(";
  }
  source += "1";
  for (int i = 0; i < depth; ++i) {
    source += ")";
  }
  source += "; return 0; }";

  const gabletest::Analysis analysis = gabletest::analyze(source);
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(gabletest::mentions(analysis, "nested too deeply"));
}

GABLE_TEST(robustness_rejects_excessive_block_nesting) {
  std::string source = "fn main() -> int { ";
  const int depth = 5000;
  for (int i = 0; i < depth; ++i) {
    source += "{ ";
  }
  for (int i = 0; i < depth; ++i) {
    source += "} ";
  }
  source += "return 0; }";

  const gabletest::Analysis analysis = gabletest::analyze(source);
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(gabletest::mentions(analysis, "nested too deeply"));
}

GABLE_TEST(robustness_accepts_deeply_but_reasonably_nested_expressions) {
  std::string source = "fn main() { let a: i32 = ";
  const int depth = 100;
  for (int i = 0; i < depth; ++i) {
    source += "(";
  }
  source += "1";
  for (int i = 0; i < depth; ++i) {
    source += ")";
  }
  source += "; print(a); }";

  const gabletest::Analysis analysis = gabletest::analyze(source);
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(robustness_handles_unterminated_constructs) {
  analyzeWithoutCrashing("fn");
  analyzeWithoutCrashing("fn main(");
  analyzeWithoutCrashing("fn main() -> {");
  analyzeWithoutCrashing("fn main() -> int { if");
  analyzeWithoutCrashing("fn main() -> int { while true {");
  analyzeWithoutCrashing("\"unterminated");
  analyzeWithoutCrashing("/* unterminated");
  analyzeWithoutCrashing("fn main() -> int { let x: i32 = 1 + ");
}

GABLE_TEST(robustness_rejects_null_bytes_in_source) {
  std::string source = "fn main() -> int { return 0; }";
  source.push_back('\0');
  source += "garbage";
  const gabletest::Analysis analysis = gabletest::analyze(source);
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(gabletest::mentions(analysis, "NUL byte is not allowed"));
}

GABLE_TEST(robustness_handles_very_long_identifier) {
  const std::string name(10000, 'a');
  const std::string source = "fn " + name + "() -> i32 { return 0; }\n"
                             "fn main() { print(" + name + "()); }\n";
  const gabletest::Analysis analysis = gabletest::analyze(source);
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(robustness_handles_large_integer_literals) {
  analyzeWithoutCrashing("fn main() -> int { return 99999999999999999999999999; }");
  analyzeWithoutCrashing("fn main() -> int { return 0xFFFFFFFFFFFFFFFFFF; }");
  analyzeWithoutCrashing("fn main() -> int { return 0b111111111111111111111111111111111111111111111111111111111111111111; }");
}

// Left-associative operator chains are parsed in a loop, so the recursion guard
// alone never fires: the tree grows by one level per operator. Without a bound on
// the tree itself, a long chain crashed the analyser, the code generator, and the
// destructor with a stack overflow.
GABLE_TEST(robustness_rejects_very_long_operator_chains) {
  std::string chain = "1";
  for (int i = 0; i < 20000; ++i) {
    chain += " + 1";
  }
  const gabletest::Analysis sum =
      gabletest::analyze("fn main() -> int {\n    let x: i64 = " + chain + ";\n    return 0;\n}\n");
  GABLE_CHECK(!sum.ok());
  GABLE_CHECK(gabletest::mentions(sum, "expression is too large"));

  // The same length reached through comparisons, with a different operator
  // precedence, is bounded the same way.
  std::string comparisons = "true";
  for (int i = 0; i < 20000; ++i) {
    comparisons += " && true";
  }
  const gabletest::Analysis logical = gabletest::analyze(
      "fn main() -> int {\n    let x: bool = " + comparisons + ";\n    return 0;\n}\n");
  GABLE_CHECK(!logical.ok());
  GABLE_CHECK(gabletest::mentions(logical, "too large") ||
              gabletest::mentions(logical, "nested too deeply"));
}

GABLE_TEST(robustness_rejects_deeply_nested_array_types) {
  std::string type = "i32";
  for (int i = 0; i < 50000; ++i) {
    type = "[" + type + "; 1]";
  }
  const gabletest::Analysis analysis = gabletest::analyze(
      "fn main() -> int {\n    let x: " + type + ";\n    return 0;\n}\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(gabletest::mentions(analysis, "nested too deeply"));
}

GABLE_TEST(robustness_rejects_long_else_if_chains) {
  // `else if` links a chain of statements without passing through a block, so
  // each link is one more frame in the parser, the analyser, and the destructor.
  std::string program = "fn main() -> int {\n    if false { return 1; }\n";
  for (int i = 0; i < 20000; ++i) {
    program += "    else if false { return 0; }\n";
  }
  program += "    return 0;\n}\n";
  const gabletest::Analysis analysis = gabletest::analyze(program);
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(gabletest::mentions(analysis, "nested too deeply"));
}

GABLE_TEST(robustness_rejects_more_than_the_source_file_limit) {
  // One root file imports every other file, so the compilation discovers one
  // more file per import without nesting. The limit must count discovered files,
  // not only the ones that finished loading.
  std::map<std::string, std::string> files;
  constexpr int kFiles = 520;
  std::string root;
  for (int index = 0; index < kFiles; ++index) {
    char name[32] = {};
    std::snprintf(name, sizeof name, "f%05d.gable", index);
    root += "import \"" + std::string(name) + "\";\n";
    files[name] = "fn f" + std::to_string(index) + "() -> i32 { return 0; }\n";
  }
  files["main.gable"] = root + "fn main() -> int { return f0(); }\n";

  const gabletest::Analysis analysis = gabletest::analyzeProject(files, "main.gable");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(gabletest::mentions(analysis, "more than 512 source files"));
}

// Loading an import recurses, so a chain of files that each import the next
// spends stack proportional to its length. The file count allows 512 files, which
// is more stack than a thread with a small stack has; the depth of the chain is
// bounded on its own.
GABLE_TEST(robustness_rejects_deeply_nested_import_chains) {
  std::map<std::string, std::string> files;
  constexpr int kFiles = 400;
  for (int index = 0; index < kFiles; ++index) {
    std::string text;
    if (index + 1 < kFiles) {
      char next[32] = {};
      std::snprintf(next, sizeof next, "f%05d.gable", index + 1);
      text = "import \"" + std::string(next) + "\";\n";
    }
    char name[32] = {};
    std::snprintf(name, sizeof name, "f%05d.gable", index);
    text += "fn f" + std::to_string(index) + "() -> i32 { return 0; }\n";
    files[name] = text;
  }
  files["main.gable"] = "import \"f00000.gable\";\nfn main() -> int { return f0(); }\n";

  const gabletest::Analysis analysis = gabletest::analyzeProject(files, "main.gable");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(gabletest::mentions(analysis, "nested more than 64 levels deep"));
}

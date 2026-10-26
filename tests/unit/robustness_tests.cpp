// Robustness tests: the compiler frontend must never crash, hang, or corrupt
// memory on malformed input. Everything here is deterministic.
#include "test_framework.h"
#include "test_helpers.h"

#include <cstdint>
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
  std::string source = "fn main() -> i32 { let a: i32 = ";
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
  std::string source = "fn main() -> i32 { ";
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
  std::string source = "fn main() -> i32 { let a: i32 = ";
  const int depth = 100;
  for (int i = 0; i < depth; ++i) {
    source += "(";
  }
  source += "1";
  for (int i = 0; i < depth; ++i) {
    source += ")";
  }
  source += "; return a; }";

  const gabletest::Analysis analysis = gabletest::analyze(source);
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(robustness_handles_unterminated_constructs) {
  analyzeWithoutCrashing("fn");
  analyzeWithoutCrashing("fn main(");
  analyzeWithoutCrashing("fn main() -> {");
  analyzeWithoutCrashing("fn main() -> i32 { if");
  analyzeWithoutCrashing("fn main() -> i32 { while true {");
  analyzeWithoutCrashing("\"unterminated");
  analyzeWithoutCrashing("/* unterminated");
  analyzeWithoutCrashing("fn main() -> i32 { let x: i32 = 1 + ");
}

GABLE_TEST(robustness_rejects_null_bytes_in_source) {
  std::string source = "fn main() -> i32 { return 0; }";
  source.push_back('\0');
  source += "garbage";
  const gabletest::Analysis analysis = gabletest::analyze(source);
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(gabletest::mentions(analysis, "NUL byte is not allowed"));
}

GABLE_TEST(robustness_handles_very_long_identifier) {
  const std::string name(10000, 'a');
  const std::string source = "fn " + name + "() -> i32 { return 0; }\n"
                             "fn main() -> i32 { return " + name + "(); }\n";
  const gabletest::Analysis analysis = gabletest::analyze(source);
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(robustness_handles_large_integer_literals) {
  analyzeWithoutCrashing("fn main() -> i32 { return 99999999999999999999999999; }");
  analyzeWithoutCrashing("fn main() -> i32 { return 0xFFFFFFFFFFFFFFFFFF; }");
  analyzeWithoutCrashing("fn main() -> i32 { return 0b111111111111111111111111111111111111111111111111111111111111111111; }");
}

// Helpers shared by the compiler unit tests.
#ifndef GABLE_TESTS_TEST_HELPERS_H
#define GABLE_TESTS_TEST_HELPERS_H

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "gable/c_backend.h"
#include "gable/compiler.h"
#include "gable/diagnostics.h"
#include "gable/ir.h"
#include "gable/lexer.h"
#include "gable/parser.h"
#include "gable/source.h"
#include "gable/version.h"

namespace gabletest {

/// Runs the lexer over `text` and returns the tokens (plus a rendered
/// diagnostic string for assertions).
inline std::vector<gable::Token> lexAll(const std::string& text, std::string& diagnostics) {
  gable::SourceFile source("test.gable", text);
  gable::DiagnosticEngine engine(source);
  gable::Lexer lexer(source, engine);
  std::vector<gable::Token> tokens = lexer.tokenize();
  std::ostringstream out;
  engine.render(out);
  diagnostics = out.str();
  return tokens;
}

struct Analysis {
  gable::FrontendResult frontend;
  std::string rendered;

  bool ok() const { return !frontend.hasErrors(); }
  std::size_t errorCount() const {
    return frontend.diagnostics ? frontend.diagnostics->errorCount() : 0;
  }
};

/// Runs the full frontend over `text`.
inline Analysis analyze(const std::string& text) {
  Analysis analysis;
  analysis.frontend = gable::analyzeSourceText("test.gable", text);
  std::ostringstream out;
  analysis.frontend.renderDiagnostics(out);
  analysis.rendered = out.str();
  return analysis;
}

/// True when the rendered diagnostics contain `needle`.
inline bool mentions(const Analysis& analysis, std::string_view needle) {
  return analysis.rendered.find(needle) != std::string::npos;
}

/// Emits C for source that is expected to compile.
inline std::string emitCFor(const std::string& text) {
  Analysis analysis = analyze(text);
  if (!analysis.ok() || !analysis.frontend.module) {
    return std::string("/* analysis failed */\n") + analysis.rendered;
  }
  return gable::emitC(*analysis.frontend.module,
                      gable::CBackendOptions{"test.gable", gable::versionString()});
}

/// A small valid program used by several tests.
inline const char* kSimpleProgram() {
  return "fn main() -> i32 {\n"
         "    print(\"hello\");\n"
         "    return 0;\n"
         "}\n";
}

}  // namespace gabletest

#endif  // GABLE_TESTS_TEST_HELPERS_H

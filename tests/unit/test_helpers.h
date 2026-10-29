// Helpers shared by the compiler unit tests.
#ifndef GABLE_TESTS_TEST_HELPERS_H
#define GABLE_TESTS_TEST_HELPERS_H

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gable/c_backend.h"
#include "gable/compiler.h"
#include "gable/diagnostics.h"
#include "gable/ir.h"
#include "gable/lexer.h"
#include "gable/parser.h"
#include "gable/source.h"
#include "gable/temporary.h"
#include "gable/version.h"

namespace gabletest {

/// Runs the lexer over `text` and returns the tokens (plus a rendered
/// diagnostic string for assertions).
inline std::vector<gable::Token> lexAll(const std::string& text, std::string& diagnostics) {
  gable::Diagnostics collected;
  auto source = std::make_unique<gable::SourceFile>("test.gable", text);
  const std::uint32_t file = collected.addFile(std::move(source));
  gable::Lexer lexer(collected.file(file), collected, file);
  std::vector<gable::Token> tokens = lexer.tokenize();
  std::ostringstream out;
  collected.render(out);
  diagnostics = out.str();
  return tokens;
}

struct Analysis {
  gable::FrontendResult frontend;
  std::string rendered;

  bool ok() const { return !frontend.hasErrors(); }
  std::size_t errorCount() const { return frontend.diagnostics.errorCount(); }
};

inline Analysis finishAnalysis(gable::FrontendResult frontend) {
  Analysis analysis;
  analysis.frontend = std::move(frontend);
  std::ostringstream out;
  analysis.frontend.renderDiagnostics(out);
  analysis.rendered = out.str();
  return analysis;
}

/// Runs the full frontend over `text`, as a single file named `test.gable`.
inline Analysis analyze(const std::string& text) {
  return finishAnalysis(gable::analyzeSourceText("test.gable", text));
}

/// Writes every entry of `files` into a fresh temporary directory and analyses
/// the file named by `root`. Imports resolve relative to the file that contains
/// them, so multi-file programs are tested exactly as users write them.
inline Analysis analyzeProject(const std::map<std::string, std::string>& files,
                               const std::string& root) {
  std::string error;
  auto directory = gable::TemporaryDirectory::create(error);
  if (!directory) {
    throw std::runtime_error("cannot create temporary directory: " + error);
  }
  for (const auto& entry : files) {
    std::ofstream file(directory->file(entry.first), std::ios::binary | std::ios::trunc);
    file << entry.second;
    if (!file) {
      throw std::runtime_error("cannot write test source '" + entry.first + "'");
    }
  }
  return finishAnalysis(gable::analyzeSourceFile(directory->file(root)));
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

/// Emits C for a multi-file program whose root is `root`.
inline std::string emitCForProject(const std::map<std::string, std::string>& files,
                                   const std::string& root) {
  Analysis analysis = analyzeProject(files, root);
  if (!analysis.ok() || !analysis.frontend.module) {
    return std::string("/* analysis failed */\n") + analysis.rendered;
  }
  return gable::emitC(*analysis.frontend.module,
                      gable::CBackendOptions{root, gable::versionString()});
}

/// A small valid program used by several tests.
inline const char* kSimpleProgram() {
  return "fn main() -> int {\n"
         "    print(\"hello\");\n"
         "    return 0;\n"
         "}\n";
}

}  // namespace gabletest

#endif  // GABLE_TESTS_TEST_HELPERS_H

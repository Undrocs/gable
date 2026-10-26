#include "gable/compiler.h"

#include <fstream>
#include <ostream>
#include <sstream>
#include <utility>
#include <vector>

#include "gable/lexer.h"
#include "gable/parser.h"
#include "gable/sema.h"

namespace gable {
namespace {

/// Refuse absurd inputs early; source files are untrusted input.
constexpr std::uintmax_t kMaxSourceBytes = 64ull * 1024ull * 1024ull;

}  // namespace

void FrontendResult::renderDiagnostics(std::ostream& out) const {
  if (hasInputError) {
    out << "gable: error: " << inputErrorMessage << '\n';
  }
  if (diagnostics) {
    diagnostics->render(out);
  }
}

FrontendResult analyzeSourceText(std::string path, std::string text) {
  FrontendResult result;
  result.source = std::make_unique<SourceFile>(std::move(path), std::move(text));
  result.diagnostics = std::make_unique<DiagnosticEngine>(*result.source);

  Lexer lexer(*result.source, *result.diagnostics);
  std::vector<Token> tokens = lexer.tokenize();

  Parser parser(tokens, *result.diagnostics);
  ast::Program program = parser.parseProgram();

  if (result.diagnostics->hasErrors()) {
    // Do not run semantic analysis on an incomplete syntax tree: it would
    // produce misleading follow-on errors.
    return result;
  }

  SemanticAnalyzer analyzer(*result.diagnostics);
  result.module = analyzer.analyze(program);
  return result;
}

FrontendResult analyzeSourceFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    FrontendResult result;
    result.hasInputError = true;
    result.inputErrorMessage = "cannot open source file '" + path + "'";
    return result;
  }

  file.seekg(0, std::ios::end);
  const std::streampos size = file.tellg();
  if (size < 0) {
    FrontendResult result;
    result.hasInputError = true;
    result.inputErrorMessage = "cannot determine the size of '" + path + "'";
    return result;
  }
  if (static_cast<std::uintmax_t>(size) > kMaxSourceBytes) {
    FrontendResult result;
    result.hasInputError = true;
    result.inputErrorMessage = "source file '" + path + "' is larger than the 64 MiB limit";
    return result;
  }
  file.seekg(0, std::ios::beg);

  std::string text(static_cast<std::size_t>(size), '\0');
  if (size > 0) {
    file.read(text.data(), size);
    if (!file) {
      FrontendResult result;
      result.hasInputError = true;
      result.inputErrorMessage = "failed while reading '" + path + "'";
      return result;
    }
  }

  return analyzeSourceText(path, std::move(text));
}

}  // namespace gable

#include "gable/compiler.h"

#include <memory>
#include <ostream>
#include <utility>
#include <vector>

#include "gable/ast.h"
#include "gable/lexer.h"
#include "gable/loader.h"
#include "gable/parser.h"
#include "gable/prelude.h"
#include "gable/sema.h"
#include "gable/source.h"
#include "gable/token.h"

namespace gable {
namespace {

/// Parses the built-in declarations. They are Gable source so that builtin
/// result types are ordinary structs, and their file index is zero so that
/// diagnostics about the prelude are recognizable.
std::unique_ptr<ast::Program> parsePrelude(Diagnostics& diagnostics) {
  auto source = std::make_unique<SourceFile>(std::string(kPreludeFileName),
                                             std::string(kPreludeSource));
  const std::uint32_t file = diagnostics.addFile(std::move(source));

  Lexer lexer(diagnostics.file(file), diagnostics, file);
  std::vector<Token> tokens = lexer.tokenize();
  Parser parser(tokens, diagnostics, file);
  return std::make_unique<ast::Program>(parser.parseProgram());
}

void runFrontend(FrontendResult& result, LoadResult loaded, std::unique_ptr<ast::Program> prelude) {
  if (loaded.hasInputError) {
    result.hasInputError = true;
    result.inputErrorMessage = std::move(loaded.inputErrorMessage);
    result.inputPaths = std::move(loaded.inputPaths);
    return;
  }
  result.inputPaths = std::move(loaded.inputPaths);

  if (loaded.hasSyntaxErrors) {
    // Semantic analysis on an incomplete tree would only add misleading errors.
    return;
  }

  std::vector<const ast::Program*> programs;
  programs.reserve(loaded.sources.size());
  for (const LoadedSource& source : loaded.sources) {
    programs.push_back(source.program.get());
  }

  SemanticAnalyzer analyzer(result.diagnostics);
  result.module = analyzer.analyze(*prelude, programs);
}

}  // namespace

void FrontendResult::renderDiagnostics(std::ostream& out) const {
  if (hasInputError) {
    out << "gable: error: " << inputErrorMessage << '\n';
  }
  diagnostics.render(out);
}

FrontendResult analyzeSourceFile(const std::string& path) {
  FrontendResult result;
  std::unique_ptr<ast::Program> prelude = parsePrelude(result.diagnostics);
  LoadResult loaded = loadProgram(path, result.diagnostics);
  runFrontend(result, std::move(loaded), std::move(prelude));
  return result;
}

FrontendResult analyzeSourceText(std::string path, std::string text) {
  FrontendResult result;
  std::unique_ptr<ast::Program> prelude = parsePrelude(result.diagnostics);
  LoadResult loaded = loadSourceText(std::move(path), std::move(text), result.diagnostics);
  runFrontend(result, std::move(loaded), std::move(prelude));
  return result;
}

}  // namespace gable

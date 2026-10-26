// Gable compiler -- frontend driver.
//
// Reads a source file and runs the lexer, parser, semantic analysis, and
// lowering to the typed IR. It performs no I/O beyond reading the input and
// never executes, links, or writes anything based on the source.
#ifndef GABLE_COMPILER_H
#define GABLE_COMPILER_H

#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include "gable/diagnostics.h"
#include "gable/ir.h"
#include "gable/source.h"

namespace gable {

/// Result of analysing one source file.
struct FrontendResult {
  std::unique_ptr<SourceFile> source;
  std::unique_ptr<DiagnosticEngine> diagnostics;
  std::unique_ptr<ir::Module> module;

  /// Paths of every file that was read for this compilation (the main source
  /// plus its imports), in load order. The driver uses this to refuse to write
  /// an output over an input.
  std::vector<std::string> inputPaths;

  /// Set when the file could not be read at all.
  bool hasInputError = false;
  std::string inputErrorMessage;

  bool hasErrors() const { return hasInputError || (diagnostics && diagnostics->hasErrors()); }
  /// Renders collected diagnostics, including the input error when present.
  void renderDiagnostics(std::ostream& out) const;
};

/// Reads and analyses `path`. Never throws for malformed source.
FrontendResult analyzeSourceFile(const std::string& path);

/// Analyses source text that is already in memory. `path` is used in
/// diagnostics only.
FrontendResult analyzeSourceText(std::string path, std::string text);

}  // namespace gable

#endif  // GABLE_COMPILER_H

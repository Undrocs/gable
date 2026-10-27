// Gable compiler -- frontend driver.
//
// Loads a source file and its imports, then runs the lexer, parser, semantic
// analysis, and lowering to the typed IR. It performs no I/O beyond reading
// source files and never executes, links, or writes outputs.
#ifndef GABLE_COMPILER_H
#define GABLE_COMPILER_H

#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include "gable/diagnostics.h"
#include "gable/ir.h"

namespace gable {

/// Result of analysing one compilation.
///
/// The diagnostics object owns the source files of every loaded file, the
/// prelude included, and must outlive any rendering of source locations.
struct FrontendResult {
  Diagnostics diagnostics;
  std::unique_ptr<ir::Module> module;

  /// Paths of every file that was read for this compilation (the main source
  /// plus its imports), in load order. The driver refuses to write an output
  /// over any of them.
  std::vector<std::string> inputPaths;

  /// Set when the main file could not be read at all.
  bool hasInputError = false;
  std::string inputErrorMessage;

  bool hasErrors() const { return hasInputError || diagnostics.hasErrors(); }
  /// Renders collected diagnostics, including the input error when present.
  void renderDiagnostics(std::ostream& out) const;
};

/// Reads and analyses `path` and its imports. Never throws for malformed source.
FrontendResult analyzeSourceFile(const std::string& path);

/// Analyses source text that is already in memory. `path` is used in
/// diagnostics and as the base for resolving imports.
FrontendResult analyzeSourceText(std::string path, std::string text);

}  // namespace gable

#endif  // GABLE_COMPILER_H

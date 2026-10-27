// Gable compiler -- source loading and import resolution.
//
// One compilation consists of a root source file and everything it imports,
// directly or transitively. Each file is loaded and parsed exactly once, even if
// several files import it, and import cycles are reported instead of being
// followed.
#ifndef GABLE_LOADER_H
#define GABLE_LOADER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gable/ast.h"
#include "gable/diagnostics.h"

namespace gable {

struct LoadedSource {
  /// Path used in diagnostics: as written by the user for the root file, and
  /// resolved relative to the importing file for imports.
  std::string path;
  std::uint32_t file = 0;
  std::unique_ptr<ast::Program> program;
};

struct LoadResult {
  /// Files in load order; a file appears after everything it imports.
  std::vector<LoadedSource> sources;
  /// Every file that was read, in load order. The driver refuses to write an
  /// output over any of these.
  std::vector<std::string> inputPaths;
  /// Set when the root file could not be read at all.
  bool hasInputError = false;
  std::string inputErrorMessage;
  /// Set when any file contained syntax errors. Semantic analysis is skipped in
  /// that case because an incomplete tree produces misleading follow-on errors.
  bool hasSyntaxErrors = false;
};

/// Loads `rootPath` and its imports. Never throws for malformed source.
LoadResult loadProgram(const std::string& rootPath, Diagnostics& diagnostics);

/// Loads source text that is already in memory. `path` is the file name used in
/// diagnostics and as the base for resolving imports.
LoadResult loadSourceText(std::string path, std::string text, Diagnostics& diagnostics);

}  // namespace gable

#endif  // GABLE_LOADER_H

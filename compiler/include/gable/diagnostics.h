// Gable compiler -- diagnostic collection and rendering.
//
// A compilation can span several source files (the main file plus its imports),
// so diagnostics are collected centrally and each one records the file it refers
// to. Diagnostics are rendered in the order they were reported, which keeps
// related notes next to the error they explain.
#ifndef GABLE_DIAGNOSTICS_H
#define GABLE_DIAGNOSTICS_H

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include "gable/source.h"

namespace gable {

enum class Severity { Error, Warning, Note };

const char* severityName(Severity severity);

struct Diagnostic {
  Severity severity = Severity::Error;
  std::uint32_t file = 0;
  SourceSpan span;
  std::string message;
};

/// Owns the source files of one compilation and the diagnostics reported for
/// them. Not thread-safe: one instance belongs to one compilation.
class Diagnostics {
 public:
  /// Takes ownership of `file` and returns its index, used by every diagnostic
  /// and node that refers to it.
  std::uint32_t addFile(std::unique_ptr<SourceFile> file);

  const SourceFile& file(std::uint32_t index) const;
  std::uint32_t fileCount() const { return static_cast<std::uint32_t>(files_.size()); }

  void error(std::uint32_t file, SourceSpan span, std::string message);
  void warning(std::uint32_t file, SourceSpan span, std::string message);
  void note(std::uint32_t file, SourceSpan span, std::string message);

  bool hasErrors() const { return errorCount_ > 0; }
  std::size_t errorCount() const { return errorCount_; }
  std::size_t diagnosticCount() const { return diagnostics_.size(); }
  const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

  /// Renders every diagnostic as
  ///   path:line:column: severity: message
  /// followed by the offending source line and a caret.
  void render(std::ostream& out) const;

 private:
  std::vector<std::unique_ptr<SourceFile>> files_;
  std::vector<Diagnostic> diagnostics_;
  std::size_t errorCount_ = 0;
};

}  // namespace gable

#endif  // GABLE_DIAGNOSTICS_H

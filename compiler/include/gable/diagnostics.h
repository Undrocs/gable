// Gable compiler -- diagnostic collection and rendering.
#ifndef GABLE_DIAGNOSTICS_H
#define GABLE_DIAGNOSTICS_H

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

#include "gable/source.h"

namespace gable {

enum class Severity { Error, Warning, Note };

const char* severityName(Severity severity);

struct Diagnostic {
  Severity severity = Severity::Error;
  SourceSpan span;
  std::string message;
};

/// Collects diagnostics for a single source file and renders them with source
/// context. A DiagnosticEngine is intentionally not thread-safe; one instance
/// belongs to one compilation.
class DiagnosticEngine {
 public:
  explicit DiagnosticEngine(const SourceFile& source);

  void error(SourceSpan span, std::string message);
  void warning(SourceSpan span, std::string message);
  void note(SourceSpan span, std::string message);

  bool hasErrors() const { return errorCount_ > 0; }
  std::size_t errorCount() const { return errorCount_; }
  std::size_t diagnosticCount() const { return diagnostics_.size(); }
  const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

  /// Renders every diagnostic to `out` in `path:line:column: severity: message`
  /// form followed by the offending source line and a caret.
  void render(std::ostream& out) const;

 private:
  const SourceFile& source_;
  std::vector<Diagnostic> diagnostics_;
  std::size_t errorCount_ = 0;
};

}  // namespace gable

#endif  // GABLE_DIAGNOSTICS_H

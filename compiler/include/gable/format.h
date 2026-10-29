// Gable compiler -- source formatting.
//
// The formatter never moves a token from one line to another, never adds a token,
// and never removes one. It rewrites whitespace: indentation from brace depth,
// one space where two tokens need separating, no space where they do not, and at
// most one blank line in a row. That is a deliberate limit. In Gable a line break
// ends a statement, so a formatter that re-wrapped code could change what a
// program means; this one cannot, because the lines stay exactly where the
// programmer put them.
//
// Comments are part of the output: a line comment stays on its line, and a block
// comment keeps its own text, including any line breaks inside it.
#ifndef GABLE_FORMAT_H
#define GABLE_FORMAT_H

#include <string>

namespace gable {

struct FormatOutcome {
  bool ok = false;
  std::string text;
  /// Set when the source cannot be scanned (an unterminated string or comment).
  /// The text is then empty and the caller must leave the file alone.
  std::string error;
};

/// Returns `source` with normalized whitespace.
///
/// The input is expected to have passed the parser; the caller runs the front end
/// first so that a file with a syntax error is never rewritten.
FormatOutcome formatSource(const std::string& source);

}  // namespace gable

#endif  // GABLE_FORMAT_H

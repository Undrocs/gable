// Gable compiler -- handwritten lexer.
#ifndef GABLE_LEXER_H
#define GABLE_LEXER_H

#include <cstdint>
#include <vector>

#include "gable/diagnostics.h"
#include "gable/source.h"
#include "gable/token.h"

namespace gable {

/// Converts a source file into a token sequence.
///
/// The lexer never throws on malformed input; every problem is reported through
/// the DiagnosticEngine and scanning continues so that a single run can report
/// several errors. The returned vector always ends with an EndOfFile token.
class Lexer {
 public:
  Lexer(const SourceFile& source, Diagnostics& diagnostics, std::uint32_t file);

  std::vector<Token> tokenize();

 private:
  bool atEnd() const { return offset_ >= source_.text().size(); }
  char peek(std::size_t lookahead = 0) const;
  char advance();
  bool match(char expected);

  /// Skips whitespace and comments; returns true when a line break was skipped,
  /// which ends a statement in Gable.
  bool skipWhitespaceAndComments();
  void scanToken(std::vector<Token>& tokens);
  void scanNumber(std::vector<Token>& tokens);
  void scanIdentifier(std::vector<Token>& tokens);
  void scanString(std::vector<Token>& tokens);

  SourceSpan spanFrom(std::size_t begin) const { return SourceSpan{begin, offset_}; }

  const SourceFile& source_;
  Diagnostics& diagnostics_;
  std::uint32_t file_;
  std::size_t offset_ = 0;
};

}  // namespace gable

#endif  // GABLE_LEXER_H

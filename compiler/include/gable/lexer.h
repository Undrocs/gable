// Gable compiler -- handwritten lexer.
#ifndef GABLE_LEXER_H
#define GABLE_LEXER_H

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
  Lexer(const SourceFile& source, DiagnosticEngine& diagnostics);

  std::vector<Token> tokenize();

 private:
  bool atEnd() const { return offset_ >= source_.text().size(); }
  char peek(std::size_t lookahead = 0) const;
  char advance();
  bool match(char expected);

  void skipWhitespaceAndComments();
  void scanToken(std::vector<Token>& tokens);
  void scanNumber(std::vector<Token>& tokens);
  void scanIdentifier(std::vector<Token>& tokens);
  void scanString(std::vector<Token>& tokens);

  SourceSpan spanFrom(std::size_t begin) const { return SourceSpan{begin, offset_}; }

  const SourceFile& source_;
  DiagnosticEngine& diagnostics_;
  std::size_t offset_ = 0;
};

}  // namespace gable

#endif  // GABLE_LEXER_H

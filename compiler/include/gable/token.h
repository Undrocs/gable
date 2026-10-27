// Gable compiler -- token definitions.
#ifndef GABLE_TOKEN_H
#define GABLE_TOKEN_H

#include <cstdint>
#include <string>

#include "gable/source.h"

namespace gable {

enum class TokenKind {
  EndOfFile,
  Identifier,
  IntegerLiteral,
  StringLiteral,

  // Keywords.
  KwFn,
  KwLet,
  KwVar,
  KwReturn,
  KwIf,
  KwElse,
  KwWhile,
  KwTrue,
  KwFalse,
  KwStruct,
  KwImport,
  KwAs,
  KwBreak,
  KwContinue,
  KwFor,
  KwIn,

  // Types.
  KwI8,
  KwI16,
  KwI32,
  KwI64,
  KwU8,
  KwU16,
  KwU32,
  KwU64,
  KwBool,
  KwStr,
  KwVoid,

  // Punctuation and operators.
  LParen,
  RParen,
  LBrace,
  RBrace,
  LBracket,
  RBracket,
  Comma,
  Colon,
  Semicolon,
  Dot,        // member access
  DotDot,     // range separator in `for i in 0..n`
  Arrow,      // ->
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Assign,     // =
  EqualEqual,
  BangEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  AmpAmp,
  PipePipe,
  Bang,
  Amp,        // reserved for future use; the parser rejects it with a clear message
  Pipe,       // reserved for future use
};

/// Returns a human-readable name for a token kind, used in diagnostics.
const char* tokenKindName(TokenKind kind);

struct Token {
  TokenKind kind = TokenKind::EndOfFile;
  SourceSpan span;
  /// Raw source text; identifiers store their spelling, literals store the
  /// characters as written (quotes included for strings).
  std::string text;
  /// Decoded value for IntegerLiteral tokens.
  std::uint64_t integerValue = 0;
  /// True when the integer literal carried an unsigned suffix.
  bool unsignedLiteral = false;
  /// Decoded UTF-8 contents for StringLiteral tokens (escapes resolved).
  std::string stringValue;
};

}  // namespace gable

#endif  // GABLE_TOKEN_H

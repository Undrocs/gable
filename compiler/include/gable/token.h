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
  /// A string literal with no interpolation.
  StringLiteral,
  /// The text before the first `$` of an interpolated string, the text between
  /// two interpolations, and the text after the last one. The parser builds one
  /// expression from the three kinds, so the lexer never has to know the grammar.
  StringStart,
  StringPart,
  StringEnd,

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
  /// `int`, the everyday 64-bit signed integer. It is the same type as `i64`,
  /// which remains available as the explicit spelling.
  KwInt,
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
  /// True when a line break separates this token from the previous one.
  ///
  /// A line break ends a statement (see §7 of the specification), so the parser
  /// needs to know where one occurred. Recording it here keeps the rule in the
  /// lexer, which already scans the whitespace, and leaves the parser free of
  /// line arithmetic.
  bool newlineBefore = false;
};

}  // namespace gable

#endif  // GABLE_TOKEN_H

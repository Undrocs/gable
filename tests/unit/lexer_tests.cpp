#include "test_framework.h"
#include "test_helpers.h"

using gable::TokenKind;
using gabletest::lexAll;

namespace {

std::vector<TokenKind> kindsOf(const std::vector<gable::Token>& tokens) {
  std::vector<TokenKind> kinds;
  for (const gable::Token& token : tokens) {
    kinds.push_back(token.kind);
  }
  return kinds;
}

}  // namespace

GABLE_TEST(lexer_tokenizes_keywords_and_punctuation) {
  std::string diagnostics;
  const auto tokens = lexAll("fn let var return if else while true false i8 i16 i32 i64 u8 u16 u32 "
                             "u64 bool str void",
                             diagnostics);
  GABLE_CHECK_EQ(std::string(""), diagnostics);
  const std::vector<TokenKind> kinds = kindsOf(tokens);
  GABLE_CHECK_EQ(std::size_t(21), kinds.size());
  GABLE_CHECK(kinds[0] == TokenKind::KwFn);
  GABLE_CHECK(kinds[1] == TokenKind::KwLet);
  GABLE_CHECK(kinds[18] == TokenKind::KwStr);
  GABLE_CHECK(kinds[19] == TokenKind::KwVoid);
  GABLE_CHECK(kinds.back() == TokenKind::EndOfFile);
}

GABLE_TEST(lexer_tokenizes_operators_greedily) {
  std::string diagnostics;
  const auto tokens = lexAll("= == -> - < <= > >= ! != && || + * / % : ; , ( ) { } & |", diagnostics);
  GABLE_CHECK_EQ(std::string(""), diagnostics);
  const std::vector<TokenKind> kinds = kindsOf(tokens);
  GABLE_CHECK(kinds[0] == TokenKind::Assign);
  GABLE_CHECK(kinds[1] == TokenKind::EqualEqual);
  GABLE_CHECK(kinds[2] == TokenKind::Arrow);
  GABLE_CHECK(kinds[3] == TokenKind::Minus);
  GABLE_CHECK(kinds[9] == TokenKind::BangEqual);
  GABLE_CHECK(kinds[10] == TokenKind::AmpAmp);
  GABLE_CHECK(kinds[11] == TokenKind::PipePipe);
  GABLE_CHECK(kinds[kinds.size() - 3] == TokenKind::Amp);
  GABLE_CHECK(kinds[kinds.size() - 2] == TokenKind::Pipe);
}

GABLE_TEST(lexer_parses_integer_literals_and_bases) {
  std::string diagnostics;
  const auto tokens = lexAll("0 42 0x2A 0b1010 18446744073709551615", diagnostics);
  GABLE_CHECK_EQ(std::string(""), diagnostics);
  GABLE_CHECK_EQ(std::uint64_t(0), tokens[0].integerValue);
  GABLE_CHECK_EQ(std::uint64_t(42), tokens[1].integerValue);
  GABLE_CHECK_EQ(std::uint64_t(42), tokens[2].integerValue);
  GABLE_CHECK_EQ(std::uint64_t(10), tokens[3].integerValue);
  GABLE_CHECK_EQ(std::uint64_t(18446744073709551615ull), tokens[4].integerValue);
}

GABLE_TEST(lexer_reports_integer_overflow) {
  std::string diagnostics;
  lexAll("18446744073709551616", diagnostics);
  GABLE_CHECK(diagnostics.find("does not fit in 64 bits") != std::string::npos);
}

GABLE_TEST(lexer_reports_hex_literal_without_digits) {
  std::string diagnostics;
  lexAll("0x", diagnostics);
  GABLE_CHECK(diagnostics.find("hexadecimal literal requires at least one digit") != std::string::npos);
}

GABLE_TEST(lexer_reports_trailing_identifier_after_number) {
  std::string diagnostics;
  lexAll("123abc", diagnostics);
  GABLE_CHECK(diagnostics.find("invalid numeric literal") != std::string::npos);
}

GABLE_TEST(lexer_decodes_string_escapes) {
  std::string diagnostics;
  const auto tokens = lexAll("\"a\\tb\\nc\\\"d\\\\e\\rf\"", diagnostics);
  GABLE_CHECK_EQ(std::string(""), diagnostics);
  GABLE_CHECK_EQ(std::size_t(2), tokens.size());
  GABLE_CHECK_EQ(std::string("a\tb\nc\"d\\e\rf"), tokens[0].stringValue);
}

GABLE_TEST(lexer_decodes_unicode_escapes_to_utf8) {
  std::string diagnostics;
  const auto tokens = lexAll("\"\\u{41}\\u00e9\\u{1F600}\"", diagnostics);
  GABLE_CHECK_EQ(std::string(""), diagnostics);
  GABLE_CHECK_EQ(std::string("A\xc3\xa9\xf0\x9f\x98\x80"), tokens[0].stringValue);
}

GABLE_TEST(lexer_rejects_invalid_unicode_escape) {
  std::string diagnostics;
  lexAll("\"\\u{D800}\"", diagnostics);
  GABLE_CHECK(diagnostics.find("surrogate") != std::string::npos);
}

GABLE_TEST(lexer_rejects_unknown_escape) {
  std::string diagnostics;
  lexAll("\"\\q\"", diagnostics);
  GABLE_CHECK(diagnostics.find("unknown escape sequence") != std::string::npos);
}

GABLE_TEST(lexer_rejects_nul_escapes) {
  std::string diagnostics;
  lexAll("\"a\\0b\"", diagnostics);
  GABLE_CHECK(diagnostics.find("NUL characters are not supported") != std::string::npos);

  diagnostics.clear();
  lexAll("\"a\\u{0}b\"", diagnostics);
  GABLE_CHECK(diagnostics.find("NUL characters are not supported") != std::string::npos);
}

GABLE_TEST(lexer_reports_unterminated_string) {
  std::string diagnostics;
  lexAll("\"abc", diagnostics);
  GABLE_CHECK(diagnostics.find("unterminated string literal") != std::string::npos);
}

GABLE_TEST(lexer_reports_unterminated_block_comment) {
  std::string diagnostics;
  lexAll("/* comment", diagnostics);
  GABLE_CHECK(diagnostics.find("unterminated block comment") != std::string::npos);
}

GABLE_TEST(lexer_supports_nested_block_comments) {
  std::string diagnostics;
  const auto tokens = lexAll("/* outer /* inner */ still a comment */ 7", diagnostics);
  GABLE_CHECK_EQ(std::string(""), diagnostics);
  GABLE_CHECK_EQ(std::size_t(2), tokens.size());
  GABLE_CHECK_EQ(std::uint64_t(7), tokens[0].integerValue);
}

GABLE_TEST(lexer_skips_line_comments) {
  std::string diagnostics;
  const auto tokens = lexAll("// nothing here\n42 // trailing\n", diagnostics);
  GABLE_CHECK_EQ(std::string(""), diagnostics);
  GABLE_CHECK_EQ(std::size_t(2), tokens.size());
}

GABLE_TEST(lexer_reports_invalid_utf8) {
  std::string diagnostics;
  lexAll(std::string("\"") + "\xff" + "\"", diagnostics);
  GABLE_CHECK(diagnostics.find("invalid UTF-8") != std::string::npos);
}

GABLE_TEST(lexer_rejects_non_ascii_identifiers) {
  std::string diagnostics;
  lexAll("let caf\xc3\xa9 = 1;", diagnostics);
  GABLE_CHECK(diagnostics.find("non-ASCII character") != std::string::npos);
}

GABLE_TEST(lexer_reports_unexpected_character) {
  std::string diagnostics;
  lexAll("let x = 1 @ 2;", diagnostics);
  GABLE_CHECK(diagnostics.find("unexpected character '@'") != std::string::npos);
}

GABLE_TEST(lexer_tracks_line_and_column) {
  const gable::SourceFile source("test.gable", "fn main() -> i32 {\n  return 0;\n}\n");
  const gable::LineColumn first = source.lineColumn(0);
  GABLE_CHECK_EQ(std::uint32_t(1), first.line);
  GABLE_CHECK_EQ(std::uint32_t(1), first.column);

  const gable::LineColumn keyword = source.lineColumn(21);  // 'r' of "return"
  GABLE_CHECK_EQ(std::uint32_t(2), keyword.line);
  GABLE_CHECK_EQ(std::uint32_t(3), keyword.column);

  const gable::LineColumn closing = source.lineColumn(31);  // the closing '}'
  GABLE_CHECK_EQ(std::uint32_t(3), closing.line);
  GABLE_CHECK_EQ(std::uint32_t(1), closing.column);
}

GABLE_TEST(lexer_counts_columns_in_code_points) {
  // "é" is two bytes; the column after it must advance by one.
  const gable::SourceFile source("test.gable", "\"\xc3\xa9\" x");
  const gable::LineColumn column = source.lineColumn(4);
  GABLE_CHECK_EQ(std::uint32_t(1), column.line);
  GABLE_CHECK_EQ(std::uint32_t(4), column.column);
}

GABLE_TEST(lexer_spans_cover_token_text) {
  std::string diagnostics;
  const auto tokens = lexAll("let value = 1234;", diagnostics);
  GABLE_CHECK_EQ(std::string("value"), tokens[1].text);
  GABLE_CHECK_EQ(std::size_t(5), tokens[1].span.end - tokens[1].span.begin);
  GABLE_CHECK_EQ(std::string("1234"), tokens[3].text);
  GABLE_CHECK_EQ(std::size_t(4), tokens[3].span.end - tokens[3].span.begin);
}

GABLE_TEST(lexer_diagnostic_caret_points_at_problem) {
  std::string diagnostics;
  lexAll("fn main() -> i32 {\n    let x = 1 @ 2;\n}\n", diagnostics);
  GABLE_CHECK(diagnostics.find("test.gable:2:15: error:") != std::string::npos);
  GABLE_CHECK(diagnostics.find("^") != std::string::npos);
}

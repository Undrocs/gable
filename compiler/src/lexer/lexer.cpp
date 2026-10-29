#include "gable/lexer.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace gable {
namespace {

struct KeywordEntry {
  std::string_view spelling;
  TokenKind kind;
};

constexpr KeywordEntry kKeywords[] = {
    {"fn", TokenKind::KwFn},         {"let", TokenKind::KwLet},
    {"var", TokenKind::KwVar},       {"return", TokenKind::KwReturn},
    {"if", TokenKind::KwIf},         {"else", TokenKind::KwElse},
    {"while", TokenKind::KwWhile},   {"true", TokenKind::KwTrue},
    {"false", TokenKind::KwFalse},   {"i8", TokenKind::KwI8},
    {"i16", TokenKind::KwI16},       {"i32", TokenKind::KwI32},
    {"i64", TokenKind::KwI64},       {"int", TokenKind::KwInt},
    {"u8", TokenKind::KwU8},
    {"u16", TokenKind::KwU16},       {"u32", TokenKind::KwU32},
    {"u64", TokenKind::KwU64},       {"bool", TokenKind::KwBool},
    {"str", TokenKind::KwStr},       {"void", TokenKind::KwVoid},
    {"struct", TokenKind::KwStruct}, {"import", TokenKind::KwImport},
    {"as", TokenKind::KwAs},         {"break", TokenKind::KwBreak},
    {"continue", TokenKind::KwContinue},
    {"for", TokenKind::KwFor},       {"in", TokenKind::KwIn},
};

bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }

bool isIdentifierStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isIdentifierContinue(char c) { return isIdentifierStart(c) || isAsciiDigit(c); }

/// Validates one UTF-8 sequence starting at `offset`. Returns the sequence
/// length, or 0 if the bytes are not valid UTF-8.
std::size_t utf8SequenceLength(std::string_view text, std::size_t offset) {
  const auto first = static_cast<unsigned char>(text[offset]);
  std::size_t length = 0;
  std::uint32_t codePoint = 0;
  if (first < 0x80) {
    return 1;
  } else if ((first & 0xE0) == 0xC0) {
    length = 2;
    codePoint = first & 0x1F;
  } else if ((first & 0xF0) == 0xE0) {
    length = 3;
    codePoint = first & 0x0F;
  } else if ((first & 0xF8) == 0xF0) {
    length = 4;
    codePoint = first & 0x07;
  } else {
    return 0;
  }
  if (offset + length > text.size()) {
    return 0;
  }
  for (std::size_t i = 1; i < length; ++i) {
    const auto byte = static_cast<unsigned char>(text[offset + i]);
    if ((byte & 0xC0) != 0x80) {
      return 0;
    }
    codePoint = (codePoint << 6) | (byte & 0x3F);
  }
  // Reject overlong encodings, surrogates, and out-of-range code points.
  if (length == 2 && codePoint < 0x80) return 0;
  if (length == 3 && codePoint < 0x800) return 0;
  if (length == 4 && codePoint < 0x10000) return 0;
  if (codePoint > 0x10FFFF) return 0;
  if (codePoint >= 0xD800 && codePoint <= 0xDFFF) return 0;
  return length;
}

void appendUtf8(std::string& out, std::uint32_t codePoint) {
  if (codePoint < 0x80) {
    out.push_back(static_cast<char>(codePoint));
  } else if (codePoint < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  } else if (codePoint < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  }
}

bool isHexDigit(char c) {
  return isAsciiDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

std::uint32_t hexValue(char c) {
  if (c >= '0' && c <= '9') return static_cast<std::uint32_t>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<std::uint32_t>(c - 'a' + 10);
  return static_cast<std::uint32_t>(c - 'A' + 10);
}

}  // namespace

const char* tokenKindName(TokenKind kind) {
  switch (kind) {
    case TokenKind::EndOfFile: return "end of file";
    case TokenKind::Identifier: return "identifier";
    case TokenKind::IntegerLiteral: return "integer literal";
    case TokenKind::StringLiteral: return "string literal";
    case TokenKind::KwFn: return "'fn'";
    case TokenKind::KwLet: return "'let'";
    case TokenKind::KwVar: return "'var'";
    case TokenKind::KwReturn: return "'return'";
    case TokenKind::KwIf: return "'if'";
    case TokenKind::KwElse: return "'else'";
    case TokenKind::KwWhile: return "'while'";
    case TokenKind::KwTrue: return "'true'";
    case TokenKind::KwFalse: return "'false'";
    case TokenKind::KwStruct: return "'struct'";
    case TokenKind::KwImport: return "'import'";
    case TokenKind::KwAs: return "'as'";
    case TokenKind::KwBreak: return "'break'";
    case TokenKind::KwContinue: return "'continue'";
    case TokenKind::KwFor: return "'for'";
    case TokenKind::KwIn: return "'in'";
    case TokenKind::KwI8: return "'i8'";
    case TokenKind::KwI16: return "'i16'";
    case TokenKind::KwI32: return "'i32'";
    case TokenKind::KwI64: return "'i64'";
    case TokenKind::KwInt: return "'int'";
    case TokenKind::KwU8: return "'u8'";
    case TokenKind::KwU16: return "'u16'";
    case TokenKind::KwU32: return "'u32'";
    case TokenKind::KwU64: return "'u64'";
    case TokenKind::KwBool: return "'bool'";
    case TokenKind::KwStr: return "'str'";
    case TokenKind::KwVoid: return "'void'";
    case TokenKind::LParen: return "'('";
    case TokenKind::RParen: return "')'";
    case TokenKind::LBrace: return "'{'";
    case TokenKind::RBrace: return "'}'";
    case TokenKind::LBracket: return "'['";
    case TokenKind::RBracket: return "']'";
    case TokenKind::Comma: return "','";
    case TokenKind::Colon: return "':'";
    case TokenKind::Semicolon: return "';'";
    case TokenKind::Dot: return "'.'";
    case TokenKind::DotDot: return "'..'";
    case TokenKind::Arrow: return "'->'";
    case TokenKind::Plus: return "'+'";
    case TokenKind::Minus: return "'-'";
    case TokenKind::Star: return "'*'";
    case TokenKind::Slash: return "'/'";
    case TokenKind::Percent: return "'%'";
    case TokenKind::Assign: return "'='";
    case TokenKind::EqualEqual: return "'=='";
    case TokenKind::BangEqual: return "'!='";
    case TokenKind::Less: return "'<'";
    case TokenKind::LessEqual: return "'<='";
    case TokenKind::Greater: return "'>'";
    case TokenKind::GreaterEqual: return "'>='";
    case TokenKind::AmpAmp: return "'&&'";
    case TokenKind::PipePipe: return "'||'";
    case TokenKind::Bang: return "'!'";
    case TokenKind::Amp: return "'&'";
    case TokenKind::Pipe: return "'|'";
  }
  return "token";
}

Lexer::Lexer(const SourceFile& source, Diagnostics& diagnostics, std::uint32_t file)
    : source_(source), diagnostics_(diagnostics), file_(file) {}

char Lexer::peek(std::size_t lookahead) const {
  const std::size_t index = offset_ + lookahead;
  return index < source_.text().size() ? source_.text()[index] : '\0';
}

char Lexer::advance() { return source_.text()[offset_++]; }

bool Lexer::match(char expected) {
  if (peek() != expected) {
    return false;
  }
  ++offset_;
  return true;
}

bool Lexer::skipWhitespaceAndComments() {
  bool sawLineBreak = false;
  for (;;) {
    const char c = peek();
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      if (c == '\n') {
        sawLineBreak = true;
      }
      ++offset_;
      continue;
    }
    if (c == '/' && peek(1) == '/') {
      while (!atEnd() && peek() != '\n') {
        ++offset_;
      }
      continue;
    }
    if (c == '/' && peek(1) == '*') {
      const std::size_t start = offset_;
      offset_ += 2;
      std::size_t depth = 1;
      while (!atEnd() && depth > 0) {
        if (peek() == '/' && peek(1) == '*') {
          offset_ += 2;
          ++depth;
        } else if (peek() == '*' && peek(1) == '/') {
          offset_ += 2;
          --depth;
        } else {
          ++offset_;
        }
      }
      if (depth > 0) {
        diagnostics_.error(file_, SourceSpan{start, offset_}, "unterminated block comment");
      }
      continue;
    }
    return sawLineBreak;
  }
}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;

  // Reject invalid UTF-8 before producing tokens so that locations stay
  // trustworthy for the rest of the file.
  for (std::size_t i = 0; i < source_.text().size();) {
    const std::size_t length = utf8SequenceLength(source_.text(), i);
    if (length == 0) {
      diagnostics_.error(file_, SourceSpan{i, i + 1}, "invalid UTF-8 byte sequence in source file");
      ++i;
    } else {
      i += length;
    }
  }

  while (!atEnd()) {
    const bool lineBreak = skipWhitespaceAndComments();
    if (atEnd()) {
      break;
    }
    const std::size_t before = tokens.size();
    scanToken(tokens);
    // `scanToken` may push more than one token (a string literal does), and all
    // of them follow the same line break.
    for (std::size_t i = before; i < tokens.size(); ++i) {
      tokens[i].newlineBefore = lineBreak;
    }
  }

  Token eof;
  eof.kind = TokenKind::EndOfFile;
  eof.span = SourceSpan{source_.text().size(), source_.text().size()};
  tokens.push_back(std::move(eof));
  return tokens;
}

void Lexer::scanToken(std::vector<Token>& tokens) {
  const std::size_t start = offset_;
  const char c = advance();

  auto push = [&](TokenKind kind) {
    Token token;
    token.kind = kind;
    token.span = spanFrom(start);
    token.text = source_.text().substr(start, offset_ - start);
    tokens.push_back(std::move(token));
  };

  switch (c) {
    case '(': push(TokenKind::LParen); return;
    case ')': push(TokenKind::RParen); return;
    case '{': push(TokenKind::LBrace); return;
    case '}': push(TokenKind::RBrace); return;
    case '[': push(TokenKind::LBracket); return;
    case ']': push(TokenKind::RBracket); return;
    case '.':
      push(match('.') ? TokenKind::DotDot : TokenKind::Dot);
      return;
    case ',': push(TokenKind::Comma); return;
    case ':': push(TokenKind::Colon); return;
    case ';': push(TokenKind::Semicolon); return;
    case '+': push(TokenKind::Plus); return;
    case '*': push(TokenKind::Star); return;
    case '/': push(TokenKind::Slash); return;
    case '%': push(TokenKind::Percent); return;
    case '-':
      push(match('>') ? TokenKind::Arrow : TokenKind::Minus);
      return;
    case '=':
      push(match('=') ? TokenKind::EqualEqual : TokenKind::Assign);
      return;
    case '!':
      push(match('=') ? TokenKind::BangEqual : TokenKind::Bang);
      return;
    case '<':
      push(match('=') ? TokenKind::LessEqual : TokenKind::Less);
      return;
    case '>':
      push(match('=') ? TokenKind::GreaterEqual : TokenKind::Greater);
      return;
    case '&':
      if (match('&')) {
        push(TokenKind::AmpAmp);
      } else {
        push(TokenKind::Amp);
      }
      return;
    case '|':
      if (match('|')) {
        push(TokenKind::PipePipe);
      } else {
        push(TokenKind::Pipe);
      }
      return;
    case '"':
      --offset_;  // scanString expects the opening quote to be unconsumed.
      scanString(tokens);
      return;
    default:
      break;
  }

  if (isAsciiDigit(c)) {
    --offset_;
    scanNumber(tokens);
    return;
  }

  if (isIdentifierStart(c)) {
    --offset_;
    scanIdentifier(tokens);
    return;
  }

  if (static_cast<unsigned char>(c) >= 0x80) {
    // A byte that is not valid UTF-8 has already been reported by tokenize();
    // skip it without a second, less precise diagnostic.
    const std::size_t length = utf8SequenceLength(source_.text(), start);
    if (length == 0) {
      return;
    }
    offset_ = start + length;
    diagnostics_.error(file_, SourceSpan{start, offset_},
                       "non-ASCII character is not allowed outside a string literal");
    return;
  }

  if (c == '\0') {
    diagnostics_.error(file_, SourceSpan{start, offset_},
                       "NUL byte is not allowed in a source file");
    return;
  }
  diagnostics_.error(file_, SourceSpan{start, offset_}, std::string("unexpected character '") + c + "'");
}

void Lexer::scanNumber(std::vector<Token>& tokens) {
  const std::size_t start = offset_;
  std::uint64_t value = 0;
  bool overflowed = false;

  auto accumulate = [&](std::uint64_t base, auto digitValue) {
    while (!atEnd() && digitValue(peek()) < base) {
      const std::uint64_t digit = digitValue(advance());
      if (value > (UINT64_MAX - digit) / base) {
        overflowed = true;
      } else {
        value = value * base + digit;
      }
    }
  };

  if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
    offset_ += 2;
    if (!isHexDigit(peek())) {
      diagnostics_.error(file_, spanFrom(start), "hexadecimal literal requires at least one digit");
    }
    accumulate(16, [](char c) -> std::uint64_t { return isHexDigit(c) ? hexValue(c) : 16; });
  } else if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
    offset_ += 2;
    if (peek() != '0' && peek() != '1') {
      diagnostics_.error(file_, spanFrom(start), "binary literal requires at least one digit");
    }
    accumulate(2, [](char c) -> std::uint64_t {
      return (c == '0' || c == '1') ? static_cast<std::uint64_t>(c - '0') : 2;
    });
  } else {
    accumulate(10, [](char c) -> std::uint64_t {
      return isAsciiDigit(c) ? static_cast<std::uint64_t>(c - '0') : 10;
    });
  }

  // An explicit unsigned suffix is lexed so that semantic analysis can reject
  // it with a precise message rather than a generic lexical error.
  bool isUnsigned = false;
  if (peek() == 'u' || peek() == 'U') {
    ++offset_;
    isUnsigned = true;
  }

  // Reject trailing identifier characters such as `123abc` rather than
  // silently splitting the token.
  if (isIdentifierContinue(peek())) {
    while (isIdentifierContinue(peek())) {
      ++offset_;
    }
    diagnostics_.error(file_, spanFrom(start),
                       "invalid numeric literal: unexpected character after digits");
  }

  if (overflowed) {
    diagnostics_.error(file_, spanFrom(start), "integer literal does not fit in 64 bits");
    value = 0;
  }

  Token token;
  token.kind = TokenKind::IntegerLiteral;
  token.span = spanFrom(start);
  token.text = source_.text().substr(start, offset_ - start);
  token.integerValue = value;
  token.unsignedLiteral = isUnsigned;
  tokens.push_back(std::move(token));
}

void Lexer::scanIdentifier(std::vector<Token>& tokens) {
  const std::size_t start = offset_;
  while (isIdentifierContinue(peek())) {
    ++offset_;
  }
  const std::string_view spelling(source_.text().data() + start, offset_ - start);
  for (const KeywordEntry& entry : kKeywords) {
    if (entry.spelling == spelling) {
      Token token;
      token.kind = entry.kind;
      token.span = spanFrom(start);
      token.text = std::string(spelling);
      tokens.push_back(std::move(token));
      return;
    }
  }
  Token token;
  token.kind = TokenKind::Identifier;
  token.span = spanFrom(start);
  token.text = std::string(spelling);
  tokens.push_back(std::move(token));
}

void Lexer::scanString(std::vector<Token>& tokens) {
  const std::size_t start = offset_;
  ++offset_;  // Consume the opening quote.
  std::string value;
  bool terminated = false;

  while (!atEnd()) {
    const std::size_t charStart = offset_;
    const char c = advance();
    if (c == '"') {
      terminated = true;
      break;
    }
    if (c == '\n') {
      break;
    }
    if (c != '\\') {
      value.push_back(c);
      continue;
    }
    if (atEnd()) {
      break;
    }
    const char escape = advance();
    switch (escape) {
      case 'n': value.push_back('\n'); break;
      case 't': value.push_back('\t'); break;
      case 'r': value.push_back('\r'); break;
      case '0':
        diagnostics_.error(file_, SourceSpan{charStart, offset_},
                           "NUL characters are not supported in string literals because 'str' is "
                           "NUL-terminated");
        break;
      case '\\': value.push_back('\\'); break;
      case '"': value.push_back('"'); break;
      case '\'': value.push_back('\''); break;
      case 'u': {
        if (peek() == '{') {
          ++offset_;
          std::uint32_t codePoint = 0;
          std::size_t digits = 0;
          while (isHexDigit(peek())) {
            codePoint = codePoint * 16 + hexValue(advance());
            ++digits;
            if (codePoint > 0x10FFFF) {
              break;
            }
          }
          if (!match('}') || digits == 0 || codePoint > 0x10FFFF ||
              (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
            diagnostics_.error(file_, SourceSpan{charStart, offset_},
                               "invalid Unicode escape; expected \\u{XXXX} with a code point "
                               "up to 10FFFF excluding surrogates");
          } else if (codePoint == 0) {
            diagnostics_.error(file_, SourceSpan{charStart, offset_},
                               "NUL characters are not supported in string literals because "
                               "'str' is NUL-terminated");
          } else {
            appendUtf8(value, codePoint);
          }
        } else if (isHexDigit(peek()) && isHexDigit(peek(1)) && isHexDigit(peek(2)) &&
                   isHexDigit(peek(3))) {
          std::uint32_t codePoint = 0;
          for (int i = 0; i < 4; ++i) {
            codePoint = codePoint * 16 + hexValue(advance());
          }
          if (codePoint >= 0xD800 && codePoint <= 0xDFFF) {
            diagnostics_.error(file_, SourceSpan{charStart, offset_},
                               "invalid Unicode escape; surrogate code points are not allowed");
          } else if (codePoint == 0) {
            diagnostics_.error(file_, SourceSpan{charStart, offset_},
                               "NUL characters are not supported in string literals because "
                               "'str' is NUL-terminated");
          } else {
            appendUtf8(value, codePoint);
          }
        } else {
          diagnostics_.error(file_, SourceSpan{charStart, offset_},
                             "invalid Unicode escape; expected \\u{XXXX} or \\uXXXX");
        }
        break;
      }
      default:
        diagnostics_.error(file_, SourceSpan{charStart, offset_},
                           std::string("unknown escape sequence '\\") + escape + "'");
        break;
    }
  }

  if (!terminated) {
    diagnostics_.error(file_, SourceSpan{start, offset_}, "unterminated string literal");
  }

  Token token;
  token.kind = TokenKind::StringLiteral;
  token.span = SourceSpan{start, offset_};
  token.text = source_.text().substr(start, offset_ - start);
  token.stringValue = std::move(value);
  tokens.push_back(std::move(token));
}

}  // namespace gable

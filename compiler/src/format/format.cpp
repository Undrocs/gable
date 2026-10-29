#include "gable/format.h"

#include <cstddef>
#include <string>
#include <vector>

namespace gable {
namespace {

constexpr int kIndentWidth = 4;

enum class TokenKind { Word, Punctuation, String, LineComment, BlockComment };

struct Piece {
  TokenKind kind = TokenKind::Punctuation;
  std::string text;
};

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isWordStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool isWordPart(char c) { return isWordStart(c) || isDigit(c); }

/// Words that are followed by a space rather than attached to a parenthesis.
bool isKeyword(const std::string& text) {
  static const char* const kKeywords[] = {"fn",   "let",  "var",  "return", "if",
                                          "else", "while", "for", "in",     "struct",
                                          "import", "as",  "break", "continue", "true",
                                          "false"};
  for (const char* keyword : kKeywords) {
    if (text == keyword) {
      return true;
    }
  }
  return false;
}

/// True when the piece ends an expression, so a following `(` is a call and a
/// following `[` is an index rather than a new literal.
bool endsExpression(const Piece& piece) {
  if (piece.kind == TokenKind::String) {
    return true;
  }
  if (piece.kind != TokenKind::Word) {
    return piece.text == ")" || piece.text == "]";
  }
  return !isKeyword(piece.text) && piece.text != "true" && piece.text != "false";
}

/// True when a `-` or `!` at this point is a prefix operator rather than a
/// binary one: nothing that ends an expression can appear to its left.
bool startsUnary(const std::vector<Piece>& line, std::size_t index) {
  for (std::size_t i = index; i-- > 0;) {
    const Piece& previous = line[i];
    if (previous.kind == TokenKind::LineComment) {
      continue;
    }
    if (previous.kind == TokenKind::Word) {
      return isKeyword(previous.text);
    }
    return !(previous.text == ")" || previous.text == "]" || previous.text == "}");
  }
  return true;
}

/// Splits one physical line into pieces. A block comment that spans several lines
/// is returned as a single piece holding its own text, so the lines inside it are
/// printed exactly as they were written. Returns false when the input cannot be
/// scanned to its end, which only happens for an unterminated string or comment.
bool scanLine(const std::string& source, std::size_t& offset, std::vector<Piece>& pieces) {
  const std::size_t size = source.size();
  while (offset < size) {
    const char c = source[offset];
    if (c == '\n') {
      return true;  // consumed by the caller
    }
    if (isSpace(c)) {
      ++offset;
      continue;
    }
    if (c == '/' && offset + 1 < size && source[offset + 1] == '/') {
      const std::size_t start = offset;
      while (offset < size && source[offset] != '\n') {
        ++offset;
      }
      pieces.push_back(Piece{TokenKind::LineComment, source.substr(start, offset - start)});
      return true;
    }
    if (c == '/' && offset + 1 < size && source[offset + 1] == '*') {
      const std::size_t start = offset;
      offset += 2;
      bool closed = false;
      while (offset < size) {
        if (source[offset] == '*' && offset + 1 < size && source[offset + 1] == '/') {
          offset += 2;
          closed = true;
          break;
        }
        ++offset;
      }
      if (!closed) {
        return false;
      }
      pieces.push_back(Piece{TokenKind::BlockComment, source.substr(start, offset - start)});
      continue;
    }
    if (c == '"') {
      const std::size_t start = offset;
      ++offset;
      bool closed = false;
      while (offset < size) {
        const char inner = source[offset];
        if (inner == '\\' && offset + 1 < size) {
          offset += 2;
          continue;
        }
        if (inner == '"') {
          ++offset;
          closed = true;
          break;
        }
        if (inner == '\n') {
          break;
        }
        ++offset;
      }
      if (!closed) {
        return false;
      }
      pieces.push_back(Piece{TokenKind::String, source.substr(start, offset - start)});
      continue;
    }
    if (isWordStart(c)) {
      const std::size_t start = offset;
      while (offset < size && isWordPart(source[offset])) {
        ++offset;
      }
      pieces.push_back(Piece{TokenKind::Word, source.substr(start, offset - start)});
      continue;
    }
    if (isDigit(c)) {
      const std::size_t start = offset;
      while (offset < size && (isWordPart(source[offset]) || source[offset] == '.')) {
        if (source[offset] == '.' && offset + 1 < size && source[offset + 1] == '.') {
          break;  // `0..n` is a range, not a literal
        }
        ++offset;
      }
      pieces.push_back(Piece{TokenKind::Word, source.substr(start, offset - start)});
      continue;
    }
    // Punctuation, longest first.
    static const char* const kOperators[] = {"..", "==", "!=", "<=", ">=", "&&", "||", "->"};
    std::string text;
    for (const char* op : kOperators) {
      const std::string candidate(op);
      if (source.compare(offset, candidate.size(), candidate) == 0) {
        text = candidate;
        break;
      }
    }
    if (text.empty()) {
      text.assign(1, c);
    }
    offset += text.size();
    pieces.push_back(Piece{TokenKind::Punctuation, text});
  }
  return true;
}

/// Whether a space is needed between two pieces written one after the other.
bool needsSpace(const std::vector<Piece>& line, std::size_t index) {
  const Piece& previous = line[index - 1];
  const Piece& next = line[index];

  if (previous.kind == TokenKind::LineComment) {
    return true;
  }
  if (next.kind == TokenKind::LineComment || next.kind == TokenKind::BlockComment) {
    return true;
  }
  if (previous.kind == TokenKind::BlockComment) {
    return true;
  }

  const std::string& left = previous.text;
  const std::string& right = next.text;

  // Attached to the token they belong to.
  if (right == "," || right == ";" || right == ")" || right == "]" || right == ":" ||
      right == "." || right == "..") {
    return false;
  }
  if (left == "(" || left == "[" || left == "." || left == "..") {
    return false;
  }
  if ((right == "(" || right == "[") && endsExpression(previous)) {
    return false;
  }
  if (left == "!" || left == "$") {
    return false;
  }
  // A prefix `-` binds to its operand; a binary one is separated on both sides.
  if (left == "-") {
    const bool unary = startsUnary(line, index - 1);
    if (unary) {
      return false;
    }
  }
  return true;
}

/// Depth changes caused by one piece. Braces, parentheses, and brackets all open
/// a level, so an expression spread over several lines is indented under the line
/// that started it.
void applyDepth(const Piece& piece, int& depth) {
  if (piece.kind != TokenKind::Punctuation) {
    return;  // strings and comments may contain brackets; they are not code
  }
  if (piece.text == "{" || piece.text == "(" || piece.text == "[") {
    ++depth;
  } else if ((piece.text == "}" || piece.text == ")" || piece.text == "]") && depth > 0) {
    --depth;
  }
}

}  // namespace

FormatOutcome formatSource(const std::string& source) {
  FormatOutcome outcome;
  std::size_t offset = 0;
  int depth = 0;
  bool wroteAnything = false;
  bool previousLineBlank = false;
  std::string output;

  auto emitLine = [&](const std::string& text) {
    output += text;
    output += '\n';
  };

  while (offset < source.size()) {
    std::vector<Piece> pieces;
    const std::size_t lineStart = offset;
    if (!scanLine(source, offset, pieces)) {
      outcome.error = "unterminated string literal or comment";
      return outcome;
    }
    bool hadNewline = false;
    if (offset < source.size() && source[offset] == '\n') {
      ++offset;
      hadNewline = true;
    }
    (void)lineStart;

    if (pieces.empty()) {
      // A blank line is kept once, and never at the start of the file.
      if (wroteAnything && !previousLineBlank && hadNewline) {
        emitLine("");
        previousLineBlank = true;
      }
      if (!hadNewline) {
        break;  // end of input
      }
      continue;
    }

    // A line that starts with `}` closes blocks: its text is indented to the
    // depth of the block it closes.
    std::size_t leadingClose = 0;
    while (leadingClose < pieces.size() && pieces[leadingClose].kind == TokenKind::Punctuation &&
           (pieces[leadingClose].text == "}" || pieces[leadingClose].text == ")" ||
            pieces[leadingClose].text == "]")) {
      ++leadingClose;
    }
    const int lineDepth = depth - static_cast<int>(leadingClose);
    std::string text(static_cast<std::size_t>(lineDepth > 0 ? lineDepth : 0) * kIndentWidth, ' ');

    for (std::size_t i = 0; i < pieces.size(); ++i) {
      if (i > 0 && needsSpace(pieces, i)) {
        text += ' ';
      }
      text += pieces[i].text;
      applyDepth(pieces[i], depth);
    }
    // Trailing whitespace is never significant. A block comment keeps its own
    // text, including the spaces it ends with.
    if (pieces.back().kind != TokenKind::BlockComment) {
      const std::size_t lastNonSpace = text.find_last_not_of(" \t");
      if (lastNonSpace != std::string::npos) {
        text.erase(lastNonSpace + 1);
      }
    }
    emitLine(text);
    wroteAnything = true;
    previousLineBlank = false;
    if (!hadNewline) {
      break;
    }
  }

  outcome.ok = true;
  outcome.text = std::move(output);
  return outcome;
}

}  // namespace gable

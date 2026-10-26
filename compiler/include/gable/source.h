// Gable compiler -- source file handling and source locations.
#ifndef GABLE_SOURCE_H
#define GABLE_SOURCE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gable {

/// A half-open byte range [begin, end) inside a SourceFile.
struct SourceSpan {
  std::size_t begin = 0;
  std::size_t end = 0;
};

/// One-based line and column. Columns count Unicode scalar values, not bytes.
struct LineColumn {
  std::uint32_t line = 1;
  std::uint32_t column = 1;
};

/// A Gable source file: its text, path, and line index.
///
/// The text must be valid UTF-8. Validation is performed by the lexer so that
/// invalid byte sequences can be reported with an accurate location.
class SourceFile {
 public:
  SourceFile(std::string path, std::string text);

  const std::string& path() const { return path_; }
  const std::string& text() const { return text_; }

  /// Converts a byte offset into a one-based line/column pair.
  LineColumn lineColumn(std::size_t offset) const;

  /// Returns the full text of the line containing `line` (one-based), with the
  /// trailing line break removed.
  std::string_view lineText(std::uint32_t line) const;

  /// Byte offset of the first character of `line` (one-based).
  std::size_t lineStartOffset(std::uint32_t line) const;

 private:
  std::string path_;
  std::string text_;
  /// Byte offset of the first character of every line.
  std::vector<std::size_t> lineStarts_;
};

}  // namespace gable

#endif  // GABLE_SOURCE_H

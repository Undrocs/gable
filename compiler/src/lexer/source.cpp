#include "gable/source.h"

#include <algorithm>

namespace gable {

SourceFile::SourceFile(std::string path, std::string text)
    : path_(std::move(path)), text_(std::move(text)) {
  lineStarts_.push_back(0);
  for (std::size_t i = 0; i < text_.size(); ++i) {
    if (text_[i] == '\n') {
      lineStarts_.push_back(i + 1);
    }
  }
}

LineColumn SourceFile::lineColumn(std::size_t offset) const {
  if (offset > text_.size()) {
    offset = text_.size();
  }
  const auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), offset);
  const auto lineIndex = static_cast<std::size_t>(it - lineStarts_.begin()) - 1;

  // Count Unicode scalar values (not bytes) from the start of the line so that
  // carets line up with non-ASCII text in string literals.
  const std::size_t lineBegin = lineStarts_[lineIndex];
  std::uint32_t column = 1;
  for (std::size_t i = lineBegin; i < offset && i < text_.size(); ++i) {
    const auto byte = static_cast<unsigned char>(text_[i]);
    if ((byte & 0xC0) != 0x80) {  // Skip UTF-8 continuation bytes.
      ++column;
    }
  }
  return LineColumn{static_cast<std::uint32_t>(lineIndex + 1), column};
}

std::size_t SourceFile::lineStartOffset(std::uint32_t line) const {
  if (line == 0 || line > lineStarts_.size()) {
    return text_.size();
  }
  return lineStarts_[line - 1];
}

std::string_view SourceFile::lineText(std::uint32_t line) const {
  if (line == 0 || line > lineStarts_.size()) {
    return {};
  }
  const std::size_t begin = lineStarts_[line - 1];
  std::size_t end = (line < lineStarts_.size()) ? lineStarts_[line] : text_.size();
  while (end > begin && (text_[end - 1] == '\n' || text_[end - 1] == '\r')) {
    --end;
  }
  return std::string_view(text_).substr(begin, end - begin);
}

}  // namespace gable

#include "gable/diagnostics.h"

#include <algorithm>
#include <ostream>
#include <sstream>
#include <string>

namespace gable {
namespace {

constexpr std::size_t kTabWidth = 4;
/// Longest source line rendered in full. Longer lines are shown as a window
/// around the diagnostic, so a single enormous line cannot flood the terminal.
constexpr std::size_t kMaxRenderedLine = 160;

/// Expands tabs so that carets align with the rendered source line.
std::string expandTabs(std::string_view text, std::size_t caretByteOffset,
                       std::size_t& caretColumnOut) {
  std::string rendered;
  std::size_t column = 0;
  caretColumnOut = 0;
  bool caretPlaced = false;

  for (std::size_t i = 0; i < text.size(); ++i) {
    if (i == caretByteOffset && !caretPlaced) {
      caretColumnOut = column;
      caretPlaced = true;
    }
    if (text[i] == '\t') {
      do {
        rendered.push_back(' ');
        ++column;
      } while (column % kTabWidth != 0);
    } else {
      rendered.push_back(text[i]);
      const auto byte = static_cast<unsigned char>(text[i]);
      if ((byte & 0xC0) != 0x80) {
        ++column;
      }
    }
  }
  if (!caretPlaced) {
    caretColumnOut = column;
  }
  return rendered;
}

}  // namespace

const char* severityName(Severity severity) {
  switch (severity) {
    case Severity::Error:
      return "error";
    case Severity::Warning:
      return "warning";
    case Severity::Note:
      return "note";
  }
  return "error";
}

DiagnosticEngine::DiagnosticEngine(const SourceFile& source) : source_(source) {}

void DiagnosticEngine::error(SourceSpan span, std::string message) {
  diagnostics_.push_back(Diagnostic{Severity::Error, span, std::move(message)});
  ++errorCount_;
}

void DiagnosticEngine::warning(SourceSpan span, std::string message) {
  diagnostics_.push_back(Diagnostic{Severity::Warning, span, std::move(message)});
}

void DiagnosticEngine::note(SourceSpan span, std::string message) {
  diagnostics_.push_back(Diagnostic{Severity::Note, span, std::move(message)});
}

void DiagnosticEngine::render(std::ostream& out) const {
  for (const Diagnostic& diagnostic : diagnostics_) {
    const LineColumn start = source_.lineColumn(diagnostic.span.begin);
    out << source_.path() << ':' << start.line << ':' << start.column << ": "
        << severityName(diagnostic.severity) << ": " << diagnostic.message << '\n';

    const std::string_view line = source_.lineText(start.line);
    if (line.empty() && start.line == 0) {
      continue;
    }

    // Byte offset of the caret within the line.
    const std::size_t lineBegin = source_.lineStartOffset(start.line);
    std::size_t caretByte = diagnostic.span.begin >= lineBegin ? diagnostic.span.begin - lineBegin : 0;
    if (caretByte > line.size()) {
      caretByte = line.size();
    }

    std::size_t caretColumn = 0;
    std::string windowed(line);
    std::size_t windowedCaret = caretByte;
    std::string prefix;
    if (windowed.size() > kMaxRenderedLine) {
      // Keep 40 bytes of leading context before the caret when possible, and
      // move both window edges onto UTF-8 code point boundaries so the rendered
      // line stays valid text.
      std::size_t windowStart = caretByte > 40 ? caretByte - 40 : 0;
      while (windowStart < line.size() &&
             (static_cast<unsigned char>(line[windowStart]) & 0xC0) == 0x80) {
        ++windowStart;
      }
      std::size_t windowEnd = std::min(line.size(), windowStart + kMaxRenderedLine);
      while (windowEnd < line.size() &&
             (static_cast<unsigned char>(line[windowEnd]) & 0xC0) == 0x80) {
        ++windowEnd;
      }
      prefix = windowStart > 0 ? "..." : "";
      windowed = std::string(line.substr(windowStart, windowEnd - windowStart));
      if (windowEnd < line.size()) {
        windowed += "...";
      }
      windowedCaret = caretByte - windowStart;
      if (windowedCaret > windowed.size()) {
        windowedCaret = windowed.size();
      }
    }
    const std::string rendered = prefix + expandTabs(windowed, windowedCaret, caretColumn);
    if (!prefix.empty()) {
      caretColumn += prefix.size();
    }

    const std::string gutter = std::to_string(start.line);
    const std::string pad(gutter.size(), ' ');
    out << "  " << gutter << " | " << rendered << '\n';
    out << "  " << pad << " | " << std::string(caretColumn, ' ') << '^';
    const std::size_t spanLength = diagnostic.span.end > diagnostic.span.begin
                                       ? diagnostic.span.end - diagnostic.span.begin
                                       : 1;
    if (spanLength > 1) {
      out << std::string(std::min<std::size_t>(spanLength - 1, 80), '~');
    }
    out << '\n';
  }
}

}  // namespace gable

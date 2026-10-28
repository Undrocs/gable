#include "gable/loader.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "gable/lexer.h"
#include "gable/parser.h"
#include "gable/source.h"

namespace gable {
namespace {

namespace fs = std::filesystem;

/// Refuse absurd inputs early; source files are untrusted input.
constexpr std::uintmax_t kMaxSourceBytes = 64ull * 1024ull * 1024ull;
/// Guards against import structures that would make the loader run away even
/// without a cycle (for example a tree of files each importing the previous one
/// twice). The count is of files the compilation has discovered, including the
/// ones still being loaded, so a chain cannot slip past it.
constexpr std::size_t kMaxSourceFiles = 512;
/// How deeply imports may nest. Loading an import is recursive, so a chain of
/// files that each import the next consumes stack proportional to its length;
/// the file count alone does not keep the loader's stack bounded. Real programs
/// nest a handful of levels, and a chain this deep is a mistake worth reporting.
constexpr std::size_t kMaxImportDepth = 64;
/// Total size of all sources of one compilation. Individual files are limited
/// separately; this bounds the whole program, so a wide import graph cannot
/// exhaust memory either.
constexpr std::uintmax_t kMaxTotalSourceBytes = 256ull * 1024ull * 1024ull;
/// Size of the buffer used to read a source file.
constexpr std::streamsize kReadChunkBytes = 64 * 1024;

/// Reads a source file, enforcing the size limit on the bytes actually read
/// rather than on the size the file system reports: the reported size can be
/// stale, and for a special file it can be meaningless.
bool readSource(const fs::path& path, std::string& text, std::string& error) {
  std::error_code code;
  const std::uintmax_t reportedSize = fs::file_size(path, code);
  if (!code && reportedSize > kMaxSourceBytes) {
    error = "file is larger than 64 MiB";
    return false;
  }

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "cannot open file";
    return false;
  }

  // Read in chunks so that a small file costs a small allocation, and stop as
  // soon as the limit is passed rather than trusting the reported size.
  std::string chunk(kReadChunkBytes, '\0');
  text.clear();
  for (;;) {
    file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    const std::streamsize read = file.gcount();
    if (read > 0) {
      text.append(chunk.data(), static_cast<std::size_t>(read));
    }
    if (text.size() > kMaxSourceBytes) {
      error = "file is larger than 64 MiB";
      return false;
    }
    if (read < static_cast<std::streamsize>(chunk.size())) {
      break;
    }
  }
  if (file.bad()) {
    error = "cannot read file";
    return false;
  }
  return true;
}

/// Identity of a file for "already loaded" purposes: the resolved path, so that
/// the same file reached through a relative path, an absolute path, or a symbolic
/// link is loaded once.
std::string canonicalPath(const fs::path& diskPath) {
  std::error_code code;
  const fs::path resolved = fs::weakly_canonical(diskPath, code);
  return code ? diskPath.lexically_normal().string() : resolved.string();
}

/// Owns one traversal of the import graph.
class Loader {
 public:
  Loader(Diagnostics& diagnostics, LoadResult& result)
      : diagnostics_(diagnostics), result_(result) {}

  void loadRoot(const std::string& path) {
    const fs::path root(path);
    if (!loadFromDisk(root, path, 0, SourceSpan{}, /*isRoot=*/true)) {
      result_.hasInputError = true;
    }
  }

  void loadRootText(std::string path, std::string text) {
    totalBytes_ += text.size();
    const std::string canonical = path;
    parseFile(std::move(path), canonical, std::move(text));
  }

 private:
  /// Reads and loads `target`. `importingFile` is the file index that reported
  /// the import and `importSpan` its location; both are zero for the root, which
  /// `isRoot` distinguishes from an import made by the first file.
  bool loadFromDisk(const fs::path& target, const std::string& reportedPath,
                    std::uint32_t importingFile, SourceSpan importSpan, bool isRoot = false) {
    const std::string canonical = canonicalPath(target);

    // A file is parsed once. Importing an already loaded file a second time is
    // legal and has no effect, mirroring the include-once behaviour that makes
    // diamond-shaped import graphs usable. The check happens before the file is
    // read, so a diamond does not read the same file twice.
    if (loaded_.count(canonical) != 0) {
      return true;
    }
    if (isActive(canonical)) {
      reportCycle(canonical, importingFile, importSpan);
      return true;
    }
    if (stack_.size() >= kMaxImportDepth) {
      diagnostics_.error(importingFile, importSpan,
                         "imports are nested more than " + std::to_string(kMaxImportDepth) +
                             " levels deep; import the shared files from the root file "
                             "instead of chaining imports");
      return true;
    }
    // Count what has been discovered, not only what has finished: a chain of
    // files each importing the next is one file deeper at every step, and the
    // completed ones are counted as well.
    if (loaded_.size() + stack_.size() >= kMaxSourceFiles) {
      diagnostics_.error(importingFile, importSpan,
                         "this compilation would load more than " +
                             std::to_string(kMaxSourceFiles) +
                             " source files; split the program into fewer modules");
      return true;
    }

    std::string text;
    std::string error;
    if (!readSource(target, text, error)) {
      // A missing root is an input error; a missing import is a diagnostic with
      // a location.
      if (isRoot) {
        result_.inputErrorMessage = "cannot read '" + reportedPath + "': " + error;
        return false;
      }
      diagnostics_.error(importingFile, importSpan,
                         "cannot import '" + reportedPath + "': " + error);
      diagnostics_.note(importingFile, importSpan,
                        "imports are resolved relative to the importing file");
      return true;
    }

    totalBytes_ += text.size();
    if (totalBytes_ > kMaxTotalSourceBytes) {
      diagnostics_.error(importingFile, importSpan,
                         "the sources of this compilation total more than 256 MiB; "
                         "split the program into smaller parts");
      return true;
    }

    parseFile(target.string(), canonical, std::move(text));
    return true;
  }

  bool isActive(const std::string& canonical) const {
    for (const std::string& active : stack_) {
      if (active == canonical) {
        return true;
      }
    }
    return false;
  }

  void reportCycle(const std::string& canonical, std::uint32_t importingFile,
                   SourceSpan importSpan) {
    std::string cycle;
    bool inCycle = false;
    for (const std::string& entry : stack_) {
      if (!inCycle && entry == canonical) {
        inCycle = true;
      }
      if (inCycle) {
        cycle += entry + " -> ";
      }
    }
    cycle += canonical;
    diagnostics_.error(importingFile, importSpan, "import cycle: " + cycle);
    diagnostics_.note(importingFile, importSpan,
                      "imports must form a directed acyclic graph; move the shared "
                      "declarations into a third file that both import");
  }

  void parseFile(std::string displayPath, std::string canonical, std::string text) {
    stack_.push_back(canonical);
    result_.inputPaths.push_back(displayPath);

    auto source = std::make_unique<SourceFile>(displayPath, std::move(text));
    const std::uint32_t fileIndex = diagnostics_.addFile(std::move(source));

    Lexer lexer(diagnostics_.file(fileIndex), diagnostics_, fileIndex);
    std::vector<Token> tokens = lexer.tokenize();

    Parser parser(tokens, diagnostics_, fileIndex);
    auto program = std::make_unique<ast::Program>(parser.parseProgram());

    if (diagnostics_.hasErrors()) {
      result_.hasSyntaxErrors = true;
    }

    // Imports are resolved before the file is emitted, so a file always follows
    // the files it depends on. Import paths are relative to the importing file.
    const fs::path directory = fs::path(displayPath).parent_path();
    for (const ast::ImportDecl& import : program->imports) {
      if (import.path.empty()) {
        continue;  // already reported by the parser
      }
      const fs::path relative(import.path);
      const fs::path target = relative.is_absolute() ? relative : directory / relative;
      loadFromDisk(target, import.path, fileIndex, import.span);
    }

    LoadedSource loaded;
    loaded.path = std::move(displayPath);
    loaded.file = fileIndex;
    loaded.program = std::move(program);
    result_.sources.push_back(std::move(loaded));

    stack_.pop_back();
    loaded_.insert(std::move(canonical));
  }

  Diagnostics& diagnostics_;
  LoadResult& result_;
  std::vector<std::string> stack_;
  std::unordered_set<std::string> loaded_;
  std::uintmax_t totalBytes_ = 0;
};

}  // namespace

LoadResult loadProgram(const std::string& rootPath, Diagnostics& diagnostics) {
  LoadResult result;
  Loader loader(diagnostics, result);
  loader.loadRoot(rootPath);
  return result;
}

LoadResult loadSourceText(std::string path, std::string text, Diagnostics& diagnostics) {
  LoadResult result;
  Loader loader(diagnostics, result);
  loader.loadRootText(std::move(path), std::move(text));
  return result;
}

}  // namespace gable

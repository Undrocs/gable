#include "gable/loader.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
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
/// without a cycle (for example a tree of files each importing the previous
/// one twice).
constexpr std::size_t kMaxSourceFiles = 512;

std::string readFileOrEmpty(const fs::path& path, std::string& error) {
  std::error_code code;
  const std::uintmax_t size = fs::file_size(path, code);
  if (code) {
    error = code.message();
    return {};
  }
  if (size > kMaxSourceBytes) {
    error = "file is larger than 64 MiB";
    return {};
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "cannot open file";
    return {};
  }
  std::ostringstream contents;
  contents << file.rdbuf();
  if (file.bad()) {
    error = "cannot read file";
    return {};
  }
  return contents.str();
}

/// Owns one traversal of the import graph.
class Loader {
 public:
  Loader(Diagnostics& diagnostics, LoadResult& result)
      : diagnostics_(diagnostics), result_(result) {}

  void loadRoot(const std::string& path) {
    const fs::path root(path);
    std::string error;
    const std::string text = readFileOrEmpty(root, error);
    if (!error.empty()) {
      result_.hasInputError = true;
      result_.inputErrorMessage = "cannot read '" + path + "': " + error;
      return;
    }
    loadFile(path, root, text, 0, SourceSpan{});
  }

  void loadRootText(std::string path, std::string text) {
    loadFile(std::move(path), fs::path(), std::move(text), 0, SourceSpan{});
  }

 private:
  /// `importingFile` is the file index that reported the import; `importSpan` is
  /// the location of the import statement. Both are zero for the root file.
  void loadFile(std::string displayPath, const fs::path& diskPath, std::string text,
                std::uint32_t importingFile, SourceSpan importSpan) {
    std::string canonical;
    if (!diskPath.empty()) {
      std::error_code code;
      const fs::path resolved = fs::weakly_canonical(diskPath, code);
      canonical = code ? diskPath.lexically_normal().string() : resolved.string();
    } else {
      canonical = displayPath;
    }

    // A file is parsed once. Importing an already loaded file a second time is
    // legal and has no effect, mirroring the include-once behaviour that makes
    // diamond-shaped import graphs usable.
    if (loaded_.count(canonical) != 0) {
      return;
    }

    if (result_.sources.size() >= kMaxSourceFiles) {
      diagnostics_.error(importingFile, importSpan,
                         "this compilation would load more than " +
                             std::to_string(kMaxSourceFiles) +
                             " source files; split the program into fewer modules");
      return;
    }
    for (const std::string& active : stack_) {
      if (active == canonical) {
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
        diagnostics_.error(importingFile, importSpan,
                           "import cycle: " + cycle);
        diagnostics_.note(importingFile, importSpan,
                          "imports must form a directed acyclic graph; move the shared "
                          "declarations into a third file that both import");
        return;
      }
    }

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
    // the files it depends on.
    const fs::path directory = diskPath.empty() ? fs::path() : diskPath.parent_path();
    for (const ast::ImportDecl& import : program->imports) {
      loadImport(import, directory, fileIndex);
    }

    LoadedSource loaded;
    loaded.path = std::move(displayPath);
    loaded.file = fileIndex;
    loaded.program = std::move(program);
    result_.sources.push_back(std::move(loaded));

    stack_.pop_back();
    loaded_.insert(canonical);
  }

  void loadImport(const ast::ImportDecl& import, const fs::path& directory,
                  std::uint32_t importingFile) {
    if (import.path.empty()) {
      return;  // already reported by the parser
    }
    const fs::path relative(import.path);
    fs::path target = relative.is_absolute() ? relative : directory / relative;

    std::string error;
    const std::string text = readFileOrEmpty(target, error);
    if (!error.empty()) {
      diagnostics_.error(importingFile, import.span,
                         "cannot import '" + import.path + "': " + error);
      diagnostics_.note(importingFile, import.span,
                        "imports are resolved relative to the importing file");
      return;
    }
    loadFile(target.string(), target, std::move(text), importingFile, import.span);
  }

  Diagnostics& diagnostics_;
  LoadResult& result_;
  std::vector<std::string> stack_;
  std::unordered_set<std::string> loaded_;
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

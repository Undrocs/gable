// Gable compiler -- safe output file handling.
//
// Outputs are never written directly to a user-specified path. Instead the new
// contents are produced in a temporary file in the same directory and moved into
// place with a single rename, which is atomic on POSIX and on Windows (when the
// target does not exist or is replaced by the runtime). That guarantees an
// existing file is either untouched or completely replaced, never truncated or
// half-written, and it makes it impossible for a failed compilation to destroy
// the previous build.
#ifndef GABLE_OUTPUT_H
#define GABLE_OUTPUT_H

#include <optional>
#include <string>
#include <vector>

namespace gable {

/// Best-effort check that two paths name the same file.
///
/// Both paths are resolved through every existing component (symlinks included)
/// and compared, and when both exist the operating system is asked whether they
/// are the same file, which also catches hard links. Non-existent paths are
/// compared by their resolved lexical form, so `./out` and `out` match.
bool pathsReferToSameFile(const std::string& left, const std::string& right);

/// Returns the first path in `candidates` that refers to the same file as
/// `target`, or an empty string when none does.
std::string findInputAlias(const std::string& target, const std::vector<std::string>& candidates);

/// A file that is written atomically.
///
/// Create it with `create`, write the new contents with `write` (or, for files
/// produced by another tool such as the C compiler, hand `temporaryPath()` to
/// that tool), then call `commit`. If `commit` is never called the temporary file
/// is removed and the target is left untouched.
class AtomicOutputFile {
 public:
  /// Prepares a temporary file next to `target`.
  static std::optional<AtomicOutputFile> create(const std::string& target, std::string& error);

  AtomicOutputFile(AtomicOutputFile&& other) noexcept;
  AtomicOutputFile& operator=(AtomicOutputFile&& other) noexcept;
  AtomicOutputFile(const AtomicOutputFile&) = delete;
  AtomicOutputFile& operator=(const AtomicOutputFile&) = delete;
  ~AtomicOutputFile();

  /// Destination the file will be moved to on commit.
  const std::string& targetPath() const { return targetPath_; }
  /// Temporary path, in the same directory as the target.
  const std::string& temporaryPath() const { return temporaryPath_; }

  /// Writes `contents` to the temporary file.
  bool write(const std::string& contents, std::string& error);

  /// Moves the temporary file onto the target path.
  bool commit(std::string& error);

 private:
  AtomicOutputFile(std::string targetPath, std::string temporaryPath)
      : targetPath_(std::move(targetPath)), temporaryPath_(std::move(temporaryPath)) {}

  void discard();

  std::string targetPath_;
  std::string temporaryPath_;
  bool committed_ = false;
};

}  // namespace gable

#endif  // GABLE_OUTPUT_H

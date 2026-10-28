// Gable compiler -- safe output file handling.
//
// Outputs are never written directly to a user-specified path. Each output is
// produced inside a private staging directory created for that compilation next
// to the destination, and moved into place with a single rename, which is atomic
// on POSIX and on Windows. That guarantees an existing file is either untouched
// or completely replaced, never truncated or half-written, so a failed
// compilation cannot destroy the previous build.
//
// The staging directory has an unpredictable name and is created exclusively, and
// the file inside it is created exclusively as well. A predictable name in a
// shared directory would let another user create that name first, as a symbolic
// link or a regular file, and have the compiler write through it.
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
/// and the staging directory are removed and the target is left untouched.
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
  /// Path of the file to produce. It lives in the staging directory, which is in
  /// the same directory as the target, so the commit stays on one file system.
  const std::string& temporaryPath() const { return temporaryPath_; }
  /// Directory that contains `temporaryPath()` and nothing else.
  const std::string& stagingPath() const { return stagingPath_; }

  /// Writes `contents` to the temporary file.
  bool write(const std::string& contents, std::string& error);

  /// Moves the temporary file onto the target path.
  bool commit(std::string& error);

 private:
  AtomicOutputFile(std::string targetPath, std::string temporaryPath, std::string stagingPath);

  void discard();

  std::string targetPath_;
  std::string temporaryPath_;
  std::string stagingPath_;
  bool committed_ = false;
};

}  // namespace gable

#endif  // GABLE_OUTPUT_H

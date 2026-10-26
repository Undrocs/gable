// Gable compiler -- temporary directory handling.
#ifndef GABLE_TEMPORARY_H
#define GABLE_TEMPORARY_H

#include <optional>
#include <string>

namespace gable {

/// Owns a private temporary directory created with mkdtemp (mode 0700) under
/// TMPDIR. The directory and its contents are removed on destruction.
class TemporaryDirectory {
 public:
  static std::optional<TemporaryDirectory> create(std::string& error);

  TemporaryDirectory(TemporaryDirectory&& other) noexcept;
  TemporaryDirectory& operator=(TemporaryDirectory&& other) noexcept;
  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
  ~TemporaryDirectory();

  const std::string& path() const { return path_; }
  /// Path of `name` inside the directory. `name` must not contain a separator.
  std::string file(const std::string& name) const;

  /// Keeps the directory after destruction (used when a failure should leave
  /// artifacts behind for inspection).
  void keep() { keep_ = true; }

 private:
  explicit TemporaryDirectory(std::string path) : path_(std::move(path)) {}

  std::string path_;
  bool keep_ = false;
};

}  // namespace gable

#endif  // GABLE_TEMPORARY_H

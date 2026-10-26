#include "gable/output.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

#include "gable/platform.h"

namespace gable {
namespace {

namespace fs = std::filesystem;

/// Resolved form of `path` for comparison. Existing components (including
/// symlinks) are resolved; missing trailing components are appended lexically, so
/// this works for outputs that do not exist yet.
fs::path resolvedForComparison(const std::string& path) {
  std::error_code ignored;
  fs::path resolved = fs::weakly_canonical(fs::path(path), ignored);
  if (ignored) {
    resolved = fs::absolute(fs::path(path), ignored);
    if (ignored) {
      return fs::path(path).lexically_normal();
    }
  }
  return resolved;
}

std::string randomSuffix() {
  // Process id plus a monotonically increasing counter. Temporary files are
  // created in the target's own directory, so this is enough to avoid collisions
  // between concurrent compilations without pulling in a random source.
  static unsigned counter = 0;
  ++counter;
  return std::to_string(platform::processId()) + "-" + std::to_string(counter);
}

}  // namespace

bool pathsReferToSameFile(const std::string& left, const std::string& right) {
  if (left.empty() || right.empty()) {
    return false;
  }
  std::error_code ignored;
  // Equivalent() also matches hard links, but only when both files exist.
  if (fs::exists(left, ignored) && fs::exists(right, ignored)) {
    const bool equivalent = fs::equivalent(left, right, ignored);
    if (!ignored && equivalent) {
      return true;
    }
  }
  return resolvedForComparison(left) == resolvedForComparison(right);
}

std::string findInputAlias(const std::string& target, const std::vector<std::string>& candidates) {
  for (const std::string& candidate : candidates) {
    if (pathsReferToSameFile(target, candidate)) {
      return candidate;
    }
  }
  return {};
}

std::optional<AtomicOutputFile> AtomicOutputFile::create(const std::string& target,
                                                        std::string& error) {
  const fs::path targetPath(target);
  fs::path directory = targetPath.parent_path();
  if (directory.empty()) {
    directory = ".";
  }

  std::error_code status;
  if (!fs::is_directory(directory, status)) {
    error = "output directory '" + directory.string() + "' does not exist";
    return std::nullopt;
  }
  if (fs::is_directory(targetPath, status)) {
    error = "output path '" + target + "' is a directory";
    return std::nullopt;
  }

  const fs::path temporary = directory / ("." + targetPath.filename().string() + ".gable-tmp-" +
                                          randomSuffix());
  return AtomicOutputFile(target, temporary.string());
}

AtomicOutputFile::AtomicOutputFile(AtomicOutputFile&& other) noexcept
    : targetPath_(std::move(other.targetPath_)),
      temporaryPath_(std::move(other.temporaryPath_)),
      committed_(other.committed_) {
  other.targetPath_.clear();
  other.temporaryPath_.clear();
  other.committed_ = true;  // a moved-from object must not remove the temp file
}

AtomicOutputFile& AtomicOutputFile::operator=(AtomicOutputFile&& other) noexcept {
  if (this != &other) {
    discard();
    targetPath_ = std::move(other.targetPath_);
    temporaryPath_ = std::move(other.temporaryPath_);
    committed_ = other.committed_;
    other.targetPath_.clear();
    other.temporaryPath_.clear();
    other.committed_ = true;
  }
  return *this;
}

AtomicOutputFile::~AtomicOutputFile() { discard(); }

void AtomicOutputFile::discard() {
  if (!committed_ && !temporaryPath_.empty()) {
    std::error_code ignored;
    fs::remove(temporaryPath_, ignored);
  }
  committed_ = true;  // idempotent
}

bool AtomicOutputFile::write(const std::string& contents, std::string& error) {
  std::ofstream file(temporaryPath_, std::ios::binary | std::ios::trunc);
  if (!file) {
    error = "cannot create '" + temporaryPath_ + "': " + std::strerror(errno);
    return false;
  }
  file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  file.close();
  if (!file) {
    error = "failed while writing '" + temporaryPath_ + "'";
    return false;
  }
  return true;
}

bool AtomicOutputFile::commit(std::string& error) {
  if (temporaryPath_.empty()) {
    error = "internal error: no temporary file to commit";
    return false;
  }
  std::error_code ignored;
  if (!fs::exists(temporaryPath_, ignored)) {
    error = "internal error: the temporary file for '" + targetPath_ + "' was not produced";
    return false;
  }
  // The platform replaces the destination in one step, so a reader either sees
  // the old file or the new one.
  if (!platform::replaceFile(temporaryPath_, targetPath_, error)) {
    error = "cannot create '" + targetPath_ + "': " + error;
    std::error_code cleanup;
    fs::remove(temporaryPath_, cleanup);
    committed_ = true;  // the temporary file is handled either way
    return false;
  }
  committed_ = true;
  return true;
}

}  // namespace gable

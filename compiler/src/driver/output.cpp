#include "gable/output.h"

#include <cstdint>
#include <filesystem>
#include <random>
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

/// Unpredictable token for a staging directory name.
///
/// A predictable name is what makes a temporary file attack possible: anyone who
/// can write to the directory could create the name first, as a symbolic link or
/// a regular file. The token mixes random bits from the operating system with the
/// process id, and the caller additionally relies on exclusive creation, so a
/// collision is a retry rather than a vulnerability.
std::string uniqueToken() {
  static std::random_device device;
  static unsigned counter = 0;
  ++counter;
  const std::uint64_t bits = (static_cast<std::uint64_t>(device()) << 32) ^ device();
  static const char* kHex = "0123456789abcdef";
  std::string token;
  token.reserve(20);
  for (int shift = 60; shift >= 0; shift -= 4) {
    token.push_back(kHex[(bits >> shift) & 0x0F]);
  }
  token += "-" + std::to_string(platform::processId()) + "-" + std::to_string(counter);
  return token;
}

/// Removes a directory if it is empty. Used for the staging directory, which
/// contains only the temporary output.
void removeIfEmpty(const std::string& path) {
  if (path.empty()) {
    return;
  }
  std::error_code ignored;
  fs::remove(path, ignored);
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

  // The new file is produced inside a directory that is created for this
  // compilation alone, next to the destination so that the final move stays on
  // one file system and therefore atomic. Nothing else can add entries to that
  // directory, so no attacker-chosen name can be in the way.
  std::string stagingPath;
  std::string stagingError;
  for (int attempt = 0; attempt < 64; ++attempt) {
    const fs::path candidate = directory / (".gable-out-" + uniqueToken());
    const platform::CreateDirectoryResult created =
        platform::createDirectoryExclusive(candidate.string(), stagingError);
    if (created == platform::CreateDirectoryResult::Created) {
      stagingPath = candidate.string();
      break;
    }
    // A collision with a name another process created is worth another token;
    // any other failure would repeat. The result is compared by value because
    // the message is localised.
    if (created != platform::CreateDirectoryResult::AlreadyExists) {
      break;
    }
  }
  if (stagingPath.empty()) {
    error = "cannot prepare the output next to '" + target + "': " + stagingError;
    return std::nullopt;
  }

  std::string temporaryPath = (fs::path(stagingPath) / ("output" + platform::executableSuffix())).string();
  return AtomicOutputFile(target, std::move(temporaryPath), stagingPath);
}

AtomicOutputFile::AtomicOutputFile(std::string targetPath, std::string temporaryPath,
                                  std::string stagingPath)
    : targetPath_(std::move(targetPath)),
      temporaryPath_(std::move(temporaryPath)),
      stagingPath_(std::move(stagingPath)) {}

AtomicOutputFile::AtomicOutputFile(AtomicOutputFile&& other) noexcept
    : targetPath_(std::move(other.targetPath_)),
      temporaryPath_(std::move(other.temporaryPath_)),
      stagingPath_(std::move(other.stagingPath_)),
      committed_(other.committed_) {
  other.targetPath_.clear();
  other.temporaryPath_.clear();
  other.stagingPath_.clear();
  other.committed_ = true;  // a moved-from object must not remove anything
}

AtomicOutputFile& AtomicOutputFile::operator=(AtomicOutputFile&& other) noexcept {
  if (this != &other) {
    discard();
    targetPath_ = std::move(other.targetPath_);
    temporaryPath_ = std::move(other.temporaryPath_);
    stagingPath_ = std::move(other.stagingPath_);
    committed_ = other.committed_;
    other.targetPath_.clear();
    other.temporaryPath_.clear();
    other.stagingPath_.clear();
    other.committed_ = true;
  }
  return *this;
}

AtomicOutputFile::~AtomicOutputFile() { discard(); }

void AtomicOutputFile::discard() {
  if (!committed_) {
    std::error_code ignored;
    fs::remove(temporaryPath_, ignored);
    removeIfEmpty(stagingPath_);
  }
  committed_ = true;  // idempotent
}

bool AtomicOutputFile::write(const std::string& contents, std::string& error) {
  if (temporaryPath_.empty()) {
    error = "internal error: no temporary file to write";
    return false;
  }
  std::string platformError;
  // Exclusive creation: the path is inside a directory this compilation owns, and
  // even if that were not the case this call refuses to write through anything
  // that already exists there.
  if (!platform::writeNewFile(temporaryPath_, contents, platformError)) {
    error = "cannot write '" + targetPath_ + "': " + platformError;
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
    removeIfEmpty(stagingPath_);
    committed_ = true;  // the temporary file is handled either way
    return false;
  }
  removeIfEmpty(stagingPath_);
  committed_ = true;
  return true;
}

}  // namespace gable

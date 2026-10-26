#include "gable/temporary.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <utility>
#include <vector>

namespace gable {
namespace {

std::string temporaryRoot() {
  const char* tmpdir = std::getenv("TMPDIR");
  if (tmpdir != nullptr && tmpdir[0] != '\0') {
    return tmpdir;
  }
  return "/tmp";
}

}  // namespace

std::optional<TemporaryDirectory> TemporaryDirectory::create(std::string& error) {
  std::string root = temporaryRoot();
  if (!root.empty() && root.back() == '/') {
    root.pop_back();
  }
  std::string pattern = root + "/gable-XXXXXX";

  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');

  if (::mkdtemp(buffer.data()) == nullptr) {
    error = std::string("cannot create a temporary directory: ") + std::strerror(errno);
    return std::nullopt;
  }
  return TemporaryDirectory(std::string(buffer.data()));
}

TemporaryDirectory::TemporaryDirectory(TemporaryDirectory&& other) noexcept
    : path_(std::move(other.path_)), keep_(other.keep_) {
  other.path_.clear();
  other.keep_ = true;  // moved-from object must not remove anything
}

TemporaryDirectory& TemporaryDirectory::operator=(TemporaryDirectory&& other) noexcept {
  if (this != &other) {
    if (!path_.empty() && !keep_) {
      std::error_code ignored;
      std::filesystem::remove_all(path_, ignored);
    }
    path_ = std::move(other.path_);
    keep_ = other.keep_;
    other.path_.clear();
    other.keep_ = true;
  }
  return *this;
}

TemporaryDirectory::~TemporaryDirectory() {
  if (!path_.empty() && !keep_) {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
}

std::string TemporaryDirectory::file(const std::string& name) const {
  if (path_.empty()) {
    return name;
  }
  return path_ + "/" + name;
}

}  // namespace gable

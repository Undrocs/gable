#include "gable/temporary.h"

#include <filesystem>
#include <string>
#include <utility>

#include "gable/platform.h"

namespace gable {

std::optional<TemporaryDirectory> TemporaryDirectory::create(std::string& error) {
  const std::string path = platform::createTemporaryDirectory(error);
  if (path.empty()) {
    return std::nullopt;
  }
  return TemporaryDirectory(path);
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

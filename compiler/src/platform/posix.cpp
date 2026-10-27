// Gable compiler -- POSIX implementation of the platform abstraction.
#if !defined(__unix__) && !defined(__APPLE__)
#error "platform/posix.cpp must only be compiled on POSIX hosts"
#endif

#include "gable/platform.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace gable::platform {

std::string executablePath() {
  namespace fs = std::filesystem;
  std::error_code ignored;

#if defined(__linux__)
  // /proc/self/exe is a symlink to the running binary; it needs no environment
  // assumptions and works when the binary was invoked through PATH.
  const fs::path resolved = fs::read_symlink(fs::path("/proc/self/exe"), ignored);
  if (!ignored && !resolved.empty()) {
    return resolved.string();
  }
  return {};
#elif defined(__APPLE__)
  std::uint32_t size = 0;
  (void)::_NSGetExecutablePath(nullptr, &size);
  if (size == 0) {
    return {};
  }
  std::vector<char> buffer(size + 1, '\0');
  if (::_NSGetExecutablePath(buffer.data(), &size) != 0) {
    return {};
  }
  const fs::path resolved = fs::weakly_canonical(fs::path(buffer.data()), ignored);
  return ignored ? std::string() : resolved.string();
#else
  return {};
#endif
}

std::uint64_t processId() { return static_cast<std::uint64_t>(::getpid()); }

std::string temporaryDirectoryRoot() {
  const char* tmpdir = std::getenv("TMPDIR");
  if (tmpdir != nullptr && tmpdir[0] != '\0') {
    return tmpdir;
  }
  return "/tmp";
}

std::string createTemporaryDirectory(std::string& error) {
  std::string root = temporaryDirectoryRoot();
  while (!root.empty() && root.back() == '/') {
    root.pop_back();
  }
  std::string pattern = root + "/gable-XXXXXX";

  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  if (::mkdtemp(buffer.data()) == nullptr) {
    error = std::string("cannot create a temporary directory: ") + std::strerror(errno);
    return {};
  }
  return std::string(buffer.data());
}

std::string defaultCCompiler() { return "cc"; }

std::string executableSuffix() { return {}; }

bool replaceFile(const std::string& source, const std::string& destination, std::string& error) {
  std::error_code code;
  std::filesystem::rename(source, destination, code);
  if (code) {
    error = code.message();
    return false;
  }
  return true;
}

}  // namespace gable::platform

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

#include <fcntl.h>
#include <sys/stat.h>
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

CreateDirectoryResult createDirectoryExclusive(const std::string& path, std::string& error) {
  // 0700: only this user can create entries in the directory, so nothing else
  // can race the compiler by planting a file or a symbolic link inside it.
  if (::mkdir(path.c_str(), S_IRWXU) == 0) {
    return CreateDirectoryResult::Created;
  }
  const int errorNumber = errno;
  error = std::strerror(errorNumber);
  if (errorNumber == EEXIST) {
    return CreateDirectoryResult::AlreadyExists;
  }
  return CreateDirectoryResult::Failed;
}

bool writeNewFile(const std::string& path, const std::string& contents, std::string& error) {
  // O_EXCL is the part that matters: it fails when anything already exists at
  // the path, including a symbolic link, instead of following it.
  const int descriptor =
      ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
  if (descriptor < 0) {
    error = std::strerror(errno);
    return false;
  }

  std::size_t written = 0;
  while (written < contents.size()) {
    const ssize_t count = ::write(descriptor, contents.data() + written, contents.size() - written);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      error = std::strerror(errno);
      ::close(descriptor);
      ::unlink(path.c_str());
      return false;
    }
    written += static_cast<std::size_t>(count);
  }

  if (::close(descriptor) != 0) {
    error = std::strerror(errno);
    ::unlink(path.c_str());
    return false;
  }
  return true;
}

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

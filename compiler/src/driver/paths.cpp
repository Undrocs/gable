#include "gable/paths.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace gable {

std::string executablePath() {
  namespace fs = std::filesystem;
  std::error_code ignored;

#if defined(__linux__)
  // /proc/self/exe is a symlink to the running binary; it needs no environment
  // assumptions and works when the binary was invoked through PATH.
  const fs::path link("/proc/self/exe");
  const fs::path resolved = fs::read_symlink(link, ignored);
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

}  // namespace gable

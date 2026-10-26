// Gable compiler -- host platform abstraction.
//
// Everything that is inherently platform-specific lives behind these functions.
// The POSIX implementation is in platform/posix.cpp; a Windows implementation
// belongs in platform/windows.cpp and must provide the same contract. Nothing in
// this header leaks operating-system types.
#ifndef GABLE_PLATFORM_H
#define GABLE_PLATFORM_H

#include <cstdint>
#include <string>

namespace gable::platform {

/// Absolute path of the running `gable` executable, or an empty string when the
/// platform cannot report it.
std::string executablePath();

/// Identifier of the current process, used to make temporary names unique.
std::uint64_t processId();

/// Directory to create temporary files in: $TMPDIR when set, otherwise the
/// platform default.
std::string temporaryDirectoryRoot();

}  // namespace gable::platform

#endif  // GABLE_PLATFORM_H

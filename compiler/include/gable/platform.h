// Gable compiler -- host platform abstraction.
//
// Everything that is inherently platform-specific lives behind these functions.
// The POSIX implementation is in platform/posix.cpp and the Windows
// implementation in platform/windows.cpp; both must provide the same contract.
// Nothing in this header leaks operating-system types.
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

/// Root directory for temporary files and directories.
std::string temporaryDirectoryRoot();

/// Creates a new, empty, uniquely named directory inside
/// temporaryDirectoryRoot() and returns its path. On failure returns an empty
/// string and sets `error`.
std::string createTemporaryDirectory(std::string& error);

/// C compiler used when the caller does not choose one: "cc" on POSIX hosts and
/// "cl" on Windows, where the C compiler is the one from the Visual Studio
/// environment.
std::string defaultCCompiler();

/// File-name suffix used for executables on this platform: ".exe" on Windows,
/// empty elsewhere. Output paths are completed with it so that a user who
/// writes `-o tool` gets `tool.exe` on Windows.
std::string executableSuffix();

/// Moves `source` to `destination`, replacing an existing file there in one
/// step. On success `source` no longer exists.
bool replaceFile(const std::string& source, const std::string& destination, std::string& error);

}  // namespace gable::platform

#endif  // GABLE_PLATFORM_H

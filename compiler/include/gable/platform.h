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
/// step. On success `source` no longer exists. A destination that another
/// process has open is retried for about a second on Windows, where the system
/// refuses to replace such a file.
bool replaceFile(const std::string& source, const std::string& destination, std::string& error);

/// Outcome of createDirectoryExclusive. A collision is reported as its own
/// value rather than as a message: the message is localised, and a caller that
/// retries with a fresh name must not depend on its wording.
enum class CreateDirectoryResult {
  Created,
  AlreadyExists,
  Failed,
};

/// Creates `path` as a new directory, failing rather than reusing it when
/// anything already exists at that path. On POSIX the directory is created with
/// owner-only permissions (0700), so nothing else can add entries to it; on
/// Windows it inherits the access control list of its parent, which is the
/// user's own directory in every case where the compiler is used.
CreateDirectoryResult createDirectoryExclusive(const std::string& path, std::string& error);

/// Creates `path` and writes `contents` to it, failing when anything already
/// exists there, including a symbolic link (exclusive creation: O_EXCL on POSIX,
/// CREATE_NEW on Windows). The file is removed again if writing fails.
bool writeNewFile(const std::string& path, const std::string& contents, std::string& error);

}  // namespace gable::platform

#endif  // GABLE_PLATFORM_H

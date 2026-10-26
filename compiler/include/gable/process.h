// Gable compiler -- structured child process execution.
//
// Commands are always passed as argument vectors and executed without a shell,
// so source paths and other user-controlled values cannot be interpreted as
// shell syntax.
#ifndef GABLE_PROCESS_H
#define GABLE_PROCESS_H

#include <string>
#include <vector>

namespace gable {

struct ProcessResult {
  /// True when the child process was created and waitable.
  bool started = false;
  /// Exit status when the process exited normally. 127 signals a failed exec.
  int exitCode = 0;
  bool exitedNormally = false;
  bool signaled = false;
  int signalNumber = 0;
  std::string standardOutput;
  std::string standardError;
  /// Set when the process could not be started at all (fork failure, etc.).
  std::string error;
};

/// Runs `argv` with captured standard output and error.
ProcessResult runProcessCapturing(const std::vector<std::string>& argv);

/// Runs `argv` with standard output and error inherited from the parent.
/// Returns the child's exit status, or -1 when it was terminated by a signal.
int runProcessInheriting(const std::vector<std::string>& argv, std::string& error);

}  // namespace gable

#endif  // GABLE_PROCESS_H

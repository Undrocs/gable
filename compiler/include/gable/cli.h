// Gable compiler -- command-line interface.
//
// The CLI is a thin layer over the compiler library so that it can be tested
// without spawning processes and so that editor tooling can reuse the same
// frontend.
#ifndef GABLE_CLI_H
#define GABLE_CLI_H

#include <iosfwd>
#include <string>
#include <vector>

namespace gable {

/// Exit codes documented in docs/getting-started.md.
enum class CliExitCode {
  Success = 0,
  CompilationFailed = 1,
  UsageError = 2,
  InternalFailure = 70,
};

/// Runs one CLI invocation. `arguments` excludes the program name.
CliExitCode runCommandLine(const std::vector<std::string>& arguments, std::ostream& out,
                           std::ostream& err);

/// Writes the help text.
void printUsage(std::ostream& out);
/// Writes the version line.
void printVersion(std::ostream& out);

}  // namespace gable

#endif  // GABLE_CLI_H

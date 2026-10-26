// Gable compiler -- build pipeline: typed IR to native executable.
#ifndef GABLE_TOOLCHAIN_H
#define GABLE_TOOLCHAIN_H

#include <iosfwd>
#include <string>

namespace gable {

/// Exit statuses used by the compiler driver. They match the `gable` CLI so
/// that library callers and shell users observe the same codes.
enum class ToolchainStatus {
  Success = 0,
  CompilationFailed = 1,
  InternalFailure = 70,
};

struct BuildOptions {
  /// Destination path for `build`. Required by buildExecutable().
  std::string outputPath;
  /// 0, 1, or 2. Passed to the C compiler as -O<n>.
  int optimizationLevel = 0;
  /// C compiler executable. Empty means $GABLE_CC, then "cc".
  std::string cCompiler;
  /// Directory containing gable/rt.h. Empty means automatic discovery.
  std::string runtimeIncludeDir;
  /// Static runtime library. Empty means automatic discovery.
  std::string runtimeLibraryPath;
  /// Keep generated C and other intermediate files after success.
  bool keepTemporaryFiles = false;
  bool verbose = false;
};

struct BuildOutput {
  ToolchainStatus status = ToolchainStatus::InternalFailure;
  /// Set when status is Success and an executable was produced.
  std::string executablePath;
  /// Human-readable explanation for any non-success status.
  std::string message;
};

/// Compiles `sourcePath` into a native executable at `options.outputPath`.
///
/// Frontend diagnostics are written to `diagnostics`. On an internal failure
/// (the C toolchain is missing, or generated C does not compile) intermediate
/// files are retained and their location is reported, because such a failure is
/// a compiler defect rather than a user error.
BuildOutput buildExecutable(const std::string& sourcePath, const BuildOptions& options,
                            std::ostream& diagnostics);

/// Runs the frontend and writes the generated C17 translation unit to `out`.
/// Used by `gable build --emit-c` for inspection and by the test suite.
BuildOutput emitCTranslationUnit(const std::string& sourcePath, const BuildOptions& options,
                                 std::ostream& out, std::ostream& diagnostics);

/// Resolves the C compiler that will be used: explicit option, $GABLE_CC, "cc".
std::string resolveCCompiler(const BuildOptions& options);

}  // namespace gable

#endif  // GABLE_TOOLCHAIN_H

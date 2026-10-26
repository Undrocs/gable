#include "gable/toolchain.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>
#include <system_error>
#include <vector>

#include "gable/c_backend.h"
#include "gable/compiler.h"
#include "gable/paths.h"
#include "gable/process.h"
#include "gable/temporary.h"
#include "gable/version.h"

namespace gable {
namespace {

namespace fs = std::filesystem;

std::string environmentOr(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  if (value != nullptr && value[0] != '\0') {
    return value;
  }
  return fallback;
}

struct RuntimePaths {
  std::string includeDir;
  std::string libraryPath;
};

/// Runtime directories next to the running binary: <prefix>/bin/gable implies
/// <prefix>/include/gable/rt.h and <prefix>/lib/libgable_rt.a. This keeps an
/// installed compiler working when it is installed with a prefix that differs
/// from the one chosen at configure time.
struct ExecutableRelativePaths {
  std::string includeDir;
  std::string libraryPath;
};

ExecutableRelativePaths executableRelativeRuntime() {
  ExecutableRelativePaths paths;
  const std::string executable = executablePath();
  if (executable.empty()) {
    return paths;
  }
  const fs::path prefix = fs::path(executable).parent_path().parent_path();
  paths.includeDir = (prefix / "include").string();
  paths.libraryPath = (prefix / "lib" / "libgable_rt.a").string();
  return paths;
}

/// Runtime location resolution order: explicit options, environment variables,
/// build-tree defaults, install-tree defaults. A candidate is accepted only if
/// the files actually exist, so a stale compile-time path is never used.
bool resolveRuntimePaths(const BuildOptions& options, RuntimePaths& paths, std::string& error) {
  std::string includeDir = options.runtimeIncludeDir;
  if (includeDir.empty()) {
    const std::string fromEnvironment = environmentOr("GABLE_RUNTIME_INCLUDE", "");
    if (!fromEnvironment.empty()) {
      includeDir = fromEnvironment;
    } else {
      const ExecutableRelativePaths relative = executableRelativeRuntime();
#ifdef GABLE_BUILD_RUNTIME_INCLUDE
      if (fs::exists(fs::path(GABLE_BUILD_RUNTIME_INCLUDE) / "gable" / "rt.h")) {
        includeDir = GABLE_BUILD_RUNTIME_INCLUDE;
      }
#endif
      if (includeDir.empty() && !relative.includeDir.empty() &&
          fs::exists(fs::path(relative.includeDir) / "gable" / "rt.h")) {
        includeDir = relative.includeDir;
      }
#ifdef GABLE_INSTALL_RUNTIME_INCLUDE
      if (includeDir.empty() &&
          fs::exists(fs::path(GABLE_INSTALL_RUNTIME_INCLUDE) / "gable" / "rt.h")) {
        includeDir = GABLE_INSTALL_RUNTIME_INCLUDE;
      }
#endif
    }
  }

  std::string libraryPath = options.runtimeLibraryPath;
  if (libraryPath.empty()) {
    const std::string fromEnvironment = environmentOr("GABLE_RUNTIME_LIB", "");
    if (!fromEnvironment.empty()) {
      libraryPath = fromEnvironment;
    } else {
      const ExecutableRelativePaths relative = executableRelativeRuntime();
#ifdef GABLE_BUILD_RUNTIME_LIB
      if (fs::is_regular_file(GABLE_BUILD_RUNTIME_LIB)) {
        libraryPath = GABLE_BUILD_RUNTIME_LIB;
      }
#endif
      if (libraryPath.empty() && !relative.libraryPath.empty() &&
          fs::is_regular_file(relative.libraryPath)) {
        libraryPath = relative.libraryPath;
      }
#ifdef GABLE_INSTALL_RUNTIME_LIB
      if (libraryPath.empty() && fs::is_regular_file(GABLE_INSTALL_RUNTIME_LIB)) {
        libraryPath = GABLE_INSTALL_RUNTIME_LIB;
      }
#endif
    }
  }

  if (includeDir.empty() || !fs::exists(fs::path(includeDir) / "gable" / "rt.h")) {
    error =
        "cannot locate the Gable runtime headers (gable/rt.h). Set GABLE_RUNTIME_INCLUDE to the "
        "directory that contains gable/rt.h.";
    return false;
  }
  if (libraryPath.empty() || !fs::is_regular_file(libraryPath)) {
    error =
        "cannot locate the Gable runtime library (libgable_rt.a). Set GABLE_RUNTIME_LIB to the "
        "library file.";
    return false;
  }

  paths.includeDir = includeDir;
  paths.libraryPath = libraryPath;
  return true;
}

/// Runs the frontend, writing diagnostics on failure.
ToolchainStatus runFrontend(const std::string& sourcePath, std::ostream& diagnostics,
                            FrontendResult& result, std::string& failureMessage) {
  result = analyzeSourceFile(sourcePath);
  if (result.hasErrors()) {
    result.renderDiagnostics(diagnostics);
    failureMessage = "compilation failed";
    return ToolchainStatus::CompilationFailed;
  }
  if (!result.module) {
    diagnostics << "gable: internal error: the frontend produced no module for '" << sourcePath
                << "'\n";
    failureMessage = "internal compiler error";
    return ToolchainStatus::InternalFailure;
  }
  return ToolchainStatus::Success;
}

}  // namespace

std::string resolveCCompiler(const BuildOptions& options) {
  if (!options.cCompiler.empty()) {
    return options.cCompiler;
  }
  return environmentOr("GABLE_CC", "cc");
}

BuildOutput emitCTranslationUnit(const std::string& sourcePath, const BuildOptions& options,
                                 std::ostream& out, std::ostream& diagnostics) {
  BuildOutput output;
  (void)options;

  FrontendResult frontend;
  output.status = runFrontend(sourcePath, diagnostics, frontend, output.message);
  if (output.status != ToolchainStatus::Success) {
    return output;
  }

  const CBackendOptions backendOptions{sourcePath, versionString()};
  out << emitC(*frontend.module, backendOptions);
  if (!out) {
    diagnostics << "gable: error: failed to write the generated C translation unit\n";
    output.status = ToolchainStatus::CompilationFailed;
    output.message = "failed to write output";
    return output;
  }

  output.status = ToolchainStatus::Success;
  return output;
}

BuildOutput buildExecutable(const std::string& sourcePath, const BuildOptions& options,
                            std::ostream& diagnostics) {
  BuildOutput output;

  if (options.outputPath.empty()) {
    diagnostics << "gable: internal error: no output path was provided\n";
    output.message = "no output path";
    return output;
  }

  FrontendResult frontend;
  output.status = runFrontend(sourcePath, diagnostics, frontend, output.message);
  if (output.status != ToolchainStatus::Success) {
    return output;
  }

  RuntimePaths runtime;
  std::string runtimeError;
  if (!resolveRuntimePaths(options, runtime, runtimeError)) {
    diagnostics << "gable: error: " << runtimeError << '\n';
    output.status = ToolchainStatus::InternalFailure;
    output.message = runtimeError;
    return output;
  }

  std::string temporaryError;
  auto temporary = TemporaryDirectory::create(temporaryError);
  if (!temporary) {
    diagnostics << "gable: error: " << temporaryError << '\n';
    output.status = ToolchainStatus::InternalFailure;
    output.message = temporaryError;
    return output;
  }

  const std::string cPath = temporary->file("module.c");
  {
    std::ofstream cFile(cPath, std::ios::binary | std::ios::trunc);
    if (!cFile) {
      diagnostics << "gable: internal error: cannot write '" << cPath << "'\n";
      output.status = ToolchainStatus::InternalFailure;
      output.message = "cannot write generated C";
      return output;
    }
    const CBackendOptions backendOptions{sourcePath, versionString()};
    cFile << emitC(*frontend.module, backendOptions);
    if (!cFile) {
      diagnostics << "gable: internal error: failed while writing '" << cPath << "'\n";
      output.status = ToolchainStatus::InternalFailure;
      output.message = "cannot write generated C";
      return output;
    }
  }

  const std::string cCompiler = resolveCCompiler(options);
  std::vector<std::string> argv = {
      cCompiler,
      "-std=c17",
      "-O" + std::to_string(options.optimizationLevel),
      "-Wall",
      "-Wextra",
      "-I" + runtime.includeDir,
      cPath,
      runtime.libraryPath,
      "-o",
      options.outputPath,
  };

  if (options.verbose) {
    diagnostics << "gable: running:";
    for (const std::string& argument : argv) {
      diagnostics << ' ' << argument;
    }
    diagnostics << '\n';
  }

  const ProcessResult process = runProcessCapturing(argv);
  if (!process.started) {
    diagnostics << "gable: internal error: cannot start the C compiler: " << process.error
                << '\n';
    output.status = ToolchainStatus::InternalFailure;
    output.message = process.error;
    temporary->keep();
    diagnostics << "gable: generated C retained at " << cPath << '\n';
    return output;
  }
  if (!process.standardOutput.empty()) {
    diagnostics << process.standardOutput;
  }
  if (!process.standardError.empty()) {
    diagnostics << process.standardError;
  }

  if (!process.exitedNormally || process.exitCode != 0) {
    diagnostics << "gable: internal error: the C compiler failed while building '" << sourcePath
                << "' (exit status " << process.exitCode << ")\n"
                << "gable: this is a compiler defect, not a problem with your source file.\n";
    output.status = ToolchainStatus::InternalFailure;
    output.message = "C compiler failed";
    temporary->keep();
    diagnostics << "gable: generated C retained at " << cPath << '\n';
    return output;
  }

  if (options.keepTemporaryFiles) {
    temporary->keep();
    diagnostics << "gable: generated C retained at " << cPath << '\n';
  }

  output.status = ToolchainStatus::Success;
  output.executablePath = options.outputPath;
  return output;
}

}  // namespace gable

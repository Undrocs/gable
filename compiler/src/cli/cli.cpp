#include "gable/cli.h"

#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>
#include <vector>

#include "gable/compiler.h"
#include "gable/format.h"
#include "gable/output.h"
#include "gable/platform.h"
#include "gable/process.h"
#include "gable/temporary.h"
#include "gable/toolchain.h"
#include "gable/version.h"

namespace gable {
namespace {

namespace fs = std::filesystem;

struct ParsedCommand {
  enum class Action { Build, Run, Check, Format, Help, Version };

  Action action = Action::Help;
  std::string sourcePath;
  /// Format: every file named on the command line.
  std::vector<std::string> formatPaths;
  /// Format: report files that would change instead of writing them.
  bool checkOnly = false;
  std::string outputPath;
  std::string emitCPath;
  bool emitC = false;
  bool keepTemporaryFiles = false;
  bool verbose = false;
  int optimizationLevel = 0;
  std::vector<std::string> programArguments;
};

int exitCodeFor(ToolchainStatus status) {
  switch (status) {
    case ToolchainStatus::Success:
      return static_cast<int>(CliExitCode::Success);
    case ToolchainStatus::CompilationFailed:
      return static_cast<int>(CliExitCode::CompilationFailed);
    case ToolchainStatus::InvalidOutput:
      return static_cast<int>(CliExitCode::UsageError);
    case ToolchainStatus::InternalFailure:
      return static_cast<int>(CliExitCode::InternalFailure);
  }
  return static_cast<int>(CliExitCode::InternalFailure);
}

bool parseOptimizationLevel(const std::string& value, int& level) {
  if (value.size() != 3 || value[0] != '-' || value[1] != 'O') {
    return false;
  }
  if (value[2] < '0' || value[2] > '2') {
    return false;
  }
  level = value[2] - '0';
  return true;
}

/// Default output path for `build`: the source path without its extension.
std::string defaultOutputPath(const std::string& sourcePath) {
  fs::path path(sourcePath);
  path.replace_extension();
  return path.string();
}

/// True when the final path component has no extension: the case that receives
/// the host's executable suffix. A path with an extension is used exactly as
/// written, because the caller named the file deliberately.
bool hasNoExtension(const std::string& path) {
  const std::size_t separator = path.find_last_of("/\\");
  const std::string name =
      separator == std::string::npos ? path : path.substr(separator + 1);
  return name.find('.') == std::string::npos;
}

bool isSuccess(ToolchainStatus status) { return status == ToolchainStatus::Success; }

/// Builds and runs `parsed`. Returns the exit code the CLI should report.
CliExitCode executeRun(const ParsedCommand& parsed, std::ostream& out, std::ostream& err) {
  (void)out;
  std::string temporaryError;
  auto temporary = TemporaryDirectory::create(temporaryError);
  if (!temporary) {
    err << "gable: error: " << temporaryError << '\n';
    return CliExitCode::InternalFailure;
  }

  BuildOptions options;
  options.outputPath = temporary->file("program");
  options.optimizationLevel = parsed.optimizationLevel;
  options.keepTemporaryFiles = parsed.keepTemporaryFiles;
  options.verbose = parsed.verbose;

  const BuildOutput build = buildExecutable(parsed.sourcePath, options, err);
  if (!isSuccess(build.status)) {
    return static_cast<CliExitCode>(exitCodeFor(build.status));
  }

  std::vector<std::string> command = {build.executablePath};
  command.insert(command.end(), parsed.programArguments.begin(), parsed.programArguments.end());

  std::string processError;
  const int exitCode = runProcessInheriting(command, processError);
  if (!processError.empty()) {
    err << "gable: error: " << processError << '\n';
  }
  if (exitCode < 0) {
    return CliExitCode::InternalFailure;
  }
  return static_cast<CliExitCode>(exitCode);
}

/// Formats every file named on the command line. A file is rewritten only after
/// it parses and only through an atomic replace, so an interrupted run cannot
/// leave a half-written source file, and a program with a syntax error is never
/// rewritten.
CliExitCode executeFormat(const ParsedCommand& parsed, std::ostream& out, std::ostream& err) {
  bool changed = false;
  bool failed = false;
  for (const std::string& path : parsed.formatPaths) {
    // A file is rewritten only when it is valid: whitespace is not the problem
    // to fix in a file that does not parse.
    FrontendResult frontend = analyzeSourceFile(path);
    if (frontend.hasErrors()) {
      frontend.renderDiagnostics(err);
      if (!frontend.hasInputError) {
        err << "gable: " << frontend.diagnostics.errorCount() << " error(s) in " << path << '\n';
      }
      failed = true;
      continue;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      err << "gable: error: cannot read " << path << '\n';
      failed = true;
      continue;
    }
    std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    input.close();

    const FormatOutcome formatted = formatSource(source);
    if (!formatted.ok) {
      err << path << ": error: " << formatted.error << '\n';
      failed = true;
      continue;
    }
    if (formatted.text == source) {
      if (parsed.verbose) {
        out << path << " is already formatted\n";
      }
      continue;
    }
    if (parsed.checkOnly) {
      out << path << " would be reformatted\n";
      changed = true;
      continue;
    }
    std::string atomicError;
    auto output = AtomicOutputFile::create(path, atomicError);
    if (!output) {
      err << "gable: error: " << atomicError << '\n';
      failed = true;
      continue;
    }
    if (!output->write(formatted.text, atomicError) || !output->commit(atomicError)) {
      err << "gable: error: " << atomicError << '\n';
      failed = true;
      continue;
    }
    if (parsed.verbose) {
      out << "formatted " << path << '\n';
    }
    changed = true;
  }

  if (failed) {
    return CliExitCode::CompilationFailed;
  }
  // `--check` reports differences through the exit status, so a script or a CI
  // job can require that every file is formatted.
  return parsed.checkOnly && changed ? CliExitCode::CompilationFailed : CliExitCode::Success;
}

/// Analyses `parsed.sourcePath` without producing any output file.
CliExitCode executeCheck(const ParsedCommand& parsed, std::ostream& err) {
  FrontendResult frontend = analyzeSourceFile(parsed.sourcePath);
  if (frontend.hasErrors()) {
    frontend.renderDiagnostics(err);
    if (!frontend.hasInputError) {
      err << "gable: " << frontend.diagnostics.errorCount() << " error(s) in "
          << parsed.sourcePath << '\n';
    }
    return CliExitCode::CompilationFailed;
  }
  if (parsed.verbose) {
    err << "gable: " << parsed.sourcePath << " is valid\n";
  }
  return CliExitCode::Success;
}

CliExitCode executeBuild(const ParsedCommand& parsed, std::ostream& out, std::ostream& err) {
  BuildOptions options;
  options.optimizationLevel = parsed.optimizationLevel;
  options.keepTemporaryFiles = parsed.keepTemporaryFiles;
  options.verbose = parsed.verbose;

  if (parsed.emitC) {
    if (parsed.emitCPath.empty()) {
      const BuildOutput result = emitCTranslationUnit(parsed.sourcePath, options, out, err);
      return static_cast<CliExitCode>(exitCodeFor(result.status));
    }
    // The destination is validated by the toolchain only after the source has
    // been analysed, so a bad -o/--emit-c path can never truncate a source file.
    const BuildOutput result =
        emitCTranslationUnitToFile(parsed.sourcePath, options, parsed.emitCPath, err);
    return static_cast<CliExitCode>(exitCodeFor(result.status));
  }

  options.outputPath = parsed.outputPath.empty() ? defaultOutputPath(parsed.sourcePath)
                                                 : parsed.outputPath;
  // `-o tool` produces `tool.exe` on Windows; the suffix is part of the path
  // the toolchain writes and the CLI reports. A path that already has an
  // extension is left alone: the caller named the file deliberately, and
  // rewriting it would surprise anyone who asked for a specific name.
  const std::string suffix = platform::executableSuffix();
  std::error_code ignored;
  if (!suffix.empty() && hasNoExtension(options.outputPath) &&
      !fs::is_directory(options.outputPath, ignored)) {
    options.outputPath += suffix;
  }
  const BuildOutput result = buildExecutable(parsed.sourcePath, options, err);
  if (!isSuccess(result.status)) {
    return static_cast<CliExitCode>(exitCodeFor(result.status));
  }
  if (parsed.verbose) {
    out << "gable: produced " << result.executablePath << '\n';
  }
  return CliExitCode::Success;
}

/// Parses `arguments`. Returns false and writes to `err` on a usage error.
bool parseArguments(const std::vector<std::string>& arguments, ParsedCommand& parsed,
                    std::ostream& err) {
  if (arguments.empty()) {
    parsed.action = ParsedCommand::Action::Help;
    return true;
  }

  const std::string& command = arguments[0];
  if (command == "--help" || command == "-h" || command == "help") {
    parsed.action = ParsedCommand::Action::Help;
    return true;
  }
  if (command == "--version" || command == "-V" || command == "version") {
    parsed.action = ParsedCommand::Action::Version;
    return true;
  }
  if (command == "build") {
    parsed.action = ParsedCommand::Action::Build;
  } else if (command == "run") {
    parsed.action = ParsedCommand::Action::Run;
  } else if (command == "check") {
    parsed.action = ParsedCommand::Action::Check;
  } else if (command == "fmt" || command == "format") {
    parsed.action = ParsedCommand::Action::Format;
  } else if (!command.empty() && command[0] == '-') {
    err << "gable: error: unknown option '" << command << "'\n";
    return false;
  } else {
    err << "gable: error: unknown command '" << command << "'\n";
    return false;
  }

  bool afterDoubleDash = false;
  for (std::size_t i = 1; i < arguments.size(); ++i) {
    const std::string& argument = arguments[i];

    if (afterDoubleDash) {
      parsed.programArguments.push_back(argument);
      continue;
    }
    if (argument == "--") {
      afterDoubleDash = true;
      continue;
    }
    if (argument == "-v" || argument == "--verbose") {
      parsed.verbose = true;
      continue;
    }
    if (argument == "--check") {
      parsed.checkOnly = true;
      continue;
    }
    if (argument == "--keep-temps") {
      parsed.keepTemporaryFiles = true;
      continue;
    }
    if (argument == "--emit-c") {
      parsed.emitC = true;
      continue;
    }
    if (argument.rfind("--emit-c=", 0) == 0) {
      parsed.emitC = true;
      parsed.emitCPath = argument.substr(std::string("--emit-c=").size());
      if (parsed.emitCPath.empty()) {
        err << "gable: error: --emit-c= requires a file path\n";
        return false;
      }
      continue;
    }
    if (argument == "-o") {
      if (i + 1 >= arguments.size()) {
        err << "gable: error: -o requires a path\n";
        return false;
      }
      parsed.outputPath = arguments[++i];
      continue;
    }
    if (argument.rfind("-o", 0) == 0 && argument.size() > 2) {
      parsed.outputPath = argument.substr(2);
      continue;
    }
    int level = 0;
    if (parseOptimizationLevel(argument, level)) {
      parsed.optimizationLevel = level;
      continue;
    }
    if (argument.rfind("-O", 0) == 0) {
      err << "gable: error: unsupported optimization level '" << argument
          << "'; use -O0, -O1, or -O2\n";
      return false;
    }
    if (!argument.empty() && argument[0] == '-' && argument != "-") {
      err << "gable: error: unknown option '" << argument << "'\n";
      return false;
    }
    if (parsed.action == ParsedCommand::Action::Format) {
      parsed.formatPaths.push_back(argument);
      continue;
    }
    if (!parsed.sourcePath.empty()) {
      err << "gable: error: only one source file may be given\n";
      return false;
    }
    parsed.sourcePath = argument;
  }

  if (parsed.action == ParsedCommand::Action::Format) {
    if (parsed.formatPaths.empty()) {
      err << "gable: error: 'fmt' requires at least one source file\n";
      return false;
    }
    if (parsed.emitC || !parsed.outputPath.empty() || !parsed.programArguments.empty() ||
        parsed.keepTemporaryFiles) {
      err << "gable: error: 'fmt' only accepts '--check', '-v', and file names\n";
      return false;
    }
    return true;
  }

  if (parsed.sourcePath.empty()) {
    err << "gable: error: '" << command << "' requires a source file\n";
    return false;
  }
  if (parsed.action == ParsedCommand::Action::Run && parsed.emitC) {
    err << "gable: error: --emit-c cannot be combined with 'run'\n";
    return false;
  }
  if (parsed.action != ParsedCommand::Action::Run && !parsed.programArguments.empty()) {
    err << "gable: error: only 'run' forwards arguments after '--'\n";
    return false;
  }
  if (parsed.action == ParsedCommand::Action::Check && parsed.emitC) {
    err << "gable: error: --emit-c produces output; use 'build' for that\n";
    return false;
  }
  if (parsed.action == ParsedCommand::Action::Check && !parsed.outputPath.empty()) {
    err << "gable: error: 'check' writes no output; remove -o\n";
    return false;
  }
  if (parsed.action == ParsedCommand::Action::Run && !parsed.outputPath.empty()) {
    err << "gable: error: 'run' does not accept -o; use 'build' to keep the executable\n";
    return false;
  }
  return true;
}

}  // namespace

void printVersion(std::ostream& out) {
  out << "gable " << versionString() << " (language version " << languageVersionString() << ")\n";
}

void printUsage(std::ostream& out) {
  out << "Gable compiler " << versionString() << "\n"
      << "\n"
      << "Usage:\n"
      << "  gable --help | --version\n"
      << "  gable build <file.gable> [options]\n"
      << "  gable run <file.gable> [options] [-- program arguments]\n"
      << "  gable check <file.gable>\n"
      << "  gable fmt <file.gable>... [--check]\n"
      << "\n"
      << "Commands:\n"
      << "  build    Compile a Gable source file to a native executable.\n"
      << "  run      Compile a Gable source file and execute it, forwarding its exit status.\n"
      << "           Program arguments follow '--'.\n"
      << "  check    Type check a Gable source file without writing any output.\n"
      << "  fmt      Rewrite files with normalized whitespace; '--check' reports\n"
      << "           the files that would change and writes nothing.\n"
      << "\n"
      << "Options:\n"
      << "  -o <path>          Output path for 'build' (default: source name without extension).\n"
      << "  -O0 | -O1 | -O2    Optimization level passed to the C backend (default: -O0).\n"
      << "  --emit-c[=<path>]  Write the generated C17 translation unit instead of compiling.\n"
      << "  --keep-temps       Keep generated C and other intermediate files.\n"
      << "  --check            With 'fmt': report files that are not formatted.\n"
      << "  -v, --verbose      Print the commands that are run.\n"
      << "\n"
      << "Exit status:\n"
      << "  0   success\n"
      << "  1   compilation failed\n"
      << "  2   usage error\n"
      << "  70  internal failure (missing toolchain, invalid generated code)\n"
      << "  For 'run', the exit status of the executed program is forwarded, except that 1, 2\n"
      << "  and 70 are also used for the compiler's own failures.\n"
      << "\n"
      << "Environment:\n"
      << "  GABLE_CC               C compiler used by the backend (default: cc, or cl\n"
      << "                         on Windows).\n"
      << "  GABLE_RUNTIME_INCLUDE  Directory containing gable/rt.h.\n"
      << "  GABLE_RUNTIME_LIB      Path to the runtime library (libgable_rt.a on POSIX\n"
      << "                         hosts, gable_rt.lib on Windows).\n";
}

CliExitCode runCommandLine(const std::vector<std::string>& arguments, std::ostream& out,
                           std::ostream& err) {
  ParsedCommand parsed;
  if (!parseArguments(arguments, parsed, err)) {
    err << "gable: run 'gable --help' for usage\n";
    return CliExitCode::UsageError;
  }

  switch (parsed.action) {
    case ParsedCommand::Action::Help:
      printUsage(out);
      return CliExitCode::Success;
    case ParsedCommand::Action::Version:
      printVersion(out);
      return CliExitCode::Success;
    case ParsedCommand::Action::Check:
      return executeCheck(parsed, err);
    case ParsedCommand::Action::Format:
      return executeFormat(parsed, out, err);
    case ParsedCommand::Action::Build:
      return executeBuild(parsed, out, err);
    case ParsedCommand::Action::Run:
      return executeRun(parsed, out, err);
  }
  return CliExitCode::UsageError;
}

}  // namespace gable

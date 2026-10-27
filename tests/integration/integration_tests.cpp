// Gable integration test runner.
//
// Usage: gable_integration_tests <path-to-gable> <cases-dir> [examples-dir]
//
// Every directory under <cases-dir> is one test case:
//
//   main.gable                      source to compile
//   expected_stdout.txt             exact expected standard output (optional)
//   expected_exit.txt               expected exit status (optional, default 0)
//   expected_stderr_contains.txt    substrings that must appear on stderr (optional)
//   program_args.txt                one argument per line, passed after `--`;
//                                   %SCRATCH% expands to a per-case scratch path
//                                   so that cases can exercise file I/O
//   compile_error.txt               when present, compilation must fail with exit
//                                   status 1 and stderr must contain every line
//
// Cases without compile_error.txt are executed through `gable run` and, in
// addition, through `gable build` to verify that the produced executable
// behaves identically. Compiler warnings from the C backend fail the case.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "gable/platform.h"
#include "gable/process.h"
#include "gable/temporary.h"

namespace fs = std::filesystem;

namespace {

std::string readFile(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

std::vector<std::string> readLines(const fs::path& path) {
  std::vector<std::string> lines;
  std::ifstream file(path);
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

std::string describe(const std::string& label, const std::string& text) {
  return "\n    " + label + ": " + (text.empty() ? std::string("<empty>") : "\n" + text);
}

struct Runner {
  std::string compiler;
  std::string scratchDir;
  std::size_t passed = 0;
  std::size_t failed = 0;

  void fail(const std::string& name, const std::string& message) {
    ++failed;
    std::cerr << "FAIL " << name << "\n    " << message << "\n";
  }

  void pass() { ++passed; }

  gable::ProcessResult runCompiler(const std::vector<std::string>& arguments) {
    std::vector<std::string> command{compiler};
    command.insert(command.end(), arguments.begin(), arguments.end());
    return gable::runProcessCapturing(command);
  }

  void runCase(const fs::path& directory) {
    const std::string name = directory.filename().string();
    const fs::path source = directory / "main.gable";
    if (!fs::exists(source)) {
      fail(name, "missing main.gable");
      return;
    }

    const fs::path compileErrorFile = directory / "compile_error.txt";
    if (fs::exists(compileErrorFile)) {
      runCompileErrorCase(name, source, readLines(compileErrorFile));
      return;
    }

    std::string expectedStdout;
    if (fs::exists(directory / "expected_stdout.txt")) {
      expectedStdout = readFile(directory / "expected_stdout.txt");
    }
    int expectedExit = 0;
    if (fs::exists(directory / "expected_exit.txt")) {
      expectedExit = std::stoi(readFile(directory / "expected_exit.txt"));
    }
    const std::vector<std::string> expectedStderr =
        fs::exists(directory / "expected_stderr_contains.txt")
            ? readLines(directory / "expected_stderr_contains.txt")
            : std::vector<std::string>{};

    std::vector<std::string> programArguments;
    if (fs::exists(directory / "program_args.txt")) {
      const std::string scratchPath = scratchDir + "/" + name + "-data.txt";
      for (const std::string& line : readLines(directory / "program_args.txt")) {
        std::string argument = line;
        const std::string placeholder = "%SCRATCH%";
        const std::size_t found = argument.find(placeholder);
        if (found != std::string::npos) {
          argument.replace(found, placeholder.size(), scratchPath);
        }
        programArguments.push_back(argument);
      }
    }
    fs::remove(scratchDir + "/" + name + "-data.txt");

    // 1. `gable run`
    std::vector<std::string> runArguments = {"run", source.string()};
    if (!programArguments.empty()) {
      runArguments.push_back("--");
      runArguments.insert(runArguments.end(), programArguments.begin(), programArguments.end());
    }
    const gable::ProcessResult run = runCompiler(runArguments);
    if (!run.started) {
      fail(name, "could not start the compiler: " + run.error);
      return;
    }
    if (run.exitCode != expectedExit) {
      fail(name, "gable run exit status " + std::to_string(run.exitCode) + ", expected " +
                     std::to_string(expectedExit) + describe("stderr", run.standardError));
      return;
    }
    if (run.standardOutput != expectedStdout) {
      fail(name, "gable run stdout mismatch" + describe("expected", expectedStdout) +
                     describe("actual", run.standardOutput));
      return;
    }
    for (const std::string& needle : expectedStderr) {
      if (!contains(run.standardError, needle)) {
        fail(name, "gable run stderr does not contain '" + needle + "'" +
                       describe("stderr", run.standardError));
        return;
      }
    }
    if (expectedStderr.empty() && !run.standardError.empty()) {
      fail(name, "unexpected compiler stderr" + describe("stderr", run.standardError));
      return;
    }

    // 2. `gable build` + direct execution
    const std::string executable =
        scratchDir + "/" + name + "-exe" + gable::platform::executableSuffix();
    const gable::ProcessResult build =
        runCompiler({"build", source.string(), "-o", executable});
    if (!build.started || build.exitCode != 0) {
      fail(name, "gable build failed with exit status " + std::to_string(build.exitCode) +
                     describe("stderr", build.standardError));
      return;
    }
    if (!build.standardError.empty()) {
      fail(name, "gable build produced warnings" + describe("stderr", build.standardError));
      return;
    }
    std::vector<std::string> executionCommand = {executable};
    executionCommand.insert(executionCommand.end(), programArguments.begin(),
                            programArguments.end());
    const gable::ProcessResult execution = gable::runProcessCapturing(executionCommand);
    if (!execution.started) {
      fail(name, "could not execute built program: " + execution.error);
      return;
    }
    if (execution.exitCode != expectedExit) {
      fail(name, "built executable exit status " + std::to_string(execution.exitCode) +
                     ", expected " + std::to_string(expectedExit));
      return;
    }
    if (execution.standardOutput != expectedStdout) {
      fail(name, "built executable stdout mismatch" + describe("expected", expectedStdout) +
                     describe("actual", execution.standardOutput));
      return;
    }
    pass();
  }

  void runCompileErrorCase(const std::string& name, const fs::path& source,
                           const std::vector<std::string>& expectedMessages) {
    const std::string executable =
        scratchDir + "/" + name + "-should-not-exist" + gable::platform::executableSuffix();
    const gable::ProcessResult build =
        runCompiler({"build", source.string(), "-o", executable});
    if (!build.started) {
      fail(name, "could not start the compiler: " + build.error);
      return;
    }
    if (build.exitCode != 1) {
      fail(name, "expected compilation to fail with exit status 1, got " +
                     std::to_string(build.exitCode) + describe("stderr", build.standardError));
      return;
    }
    if (fs::exists(executable)) {
      fail(name, "compilation reported errors but produced an executable");
      return;
    }
    for (const std::string& message : expectedMessages) {
      if (!contains(build.standardError, message)) {
        fail(name, "diagnostics do not contain '" + message + "'" +
                       describe("stderr", build.standardError));
        return;
      }
    }
    pass();
  }
};

std::vector<fs::path> sortedDirectories(const fs::path& root) {
  std::vector<fs::path> directories;
  if (!fs::exists(root)) {
    return directories;
  }
  for (const fs::directory_entry& entry : fs::directory_iterator(root)) {
    if (entry.is_directory()) {
      directories.push_back(entry.path());
    }
  }
  std::sort(directories.begin(), directories.end());
  return directories;
}

std::vector<fs::path> sortedSources(const fs::path& root) {
  std::vector<fs::path> sources;
  if (!fs::exists(root)) {
    return sources;
  }
  for (const fs::directory_entry& entry : fs::directory_iterator(root)) {
    if (entry.is_regular_file() && entry.path().extension() == ".gable") {
      sources.push_back(entry.path());
    }
  }
  std::sort(sources.begin(), sources.end());
  return sources;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: gable_integration_tests <path-to-gable> <cases-dir> [examples-dir]\n";
    return 2;
  }

  std::string temporaryError;
  auto scratch = gable::TemporaryDirectory::create(temporaryError);
  if (!scratch) {
    std::cerr << "cannot create temporary directory: " << temporaryError << "\n";
    return 2;
  }

  Runner runner;
  runner.compiler = argv[1];
  runner.scratchDir = scratch->path();

  const std::vector<fs::path> cases = sortedDirectories(argv[2]);
  if (cases.empty()) {
    std::cerr << "no test cases found in " << argv[2] << "\n";
    return 2;
  }
  for (const fs::path& directory : cases) {
    runner.runCase(directory);
  }
  const std::size_t caseCount = cases.size();

  // Examples must compile, run, and exit successfully. Their printed output is
  // not compared here because the case suite covers exact output.
  std::size_t exampleCount = 0;
  if (argc > 3) {
    for (const fs::path& source : sortedSources(argv[3])) {
      ++exampleCount;
      const std::string name = "example/" + source.filename().string();
      const gable::ProcessResult run = runner.runCompiler({"run", source.string()});
      if (!run.started || run.exitCode != 0 || !run.standardError.empty()) {
        runner.fail(name, "exit status " + std::to_string(run.exitCode) +
                              describe("stderr", run.standardError));
        continue;
      }
      runner.pass();
    }
  }

  std::cout << (runner.failed == 0 ? "PASS " : "FAILED ") << runner.passed << "/"
            << (caseCount + exampleCount) << " integration tests (" << caseCount << " cases, "
            << exampleCount << " examples)\n";
  return runner.failed == 0 ? 0 : 1;
}

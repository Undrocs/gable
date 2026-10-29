#include "test_framework.h"
#include "test_helpers.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "gable/cli.h"
#include "gable/output.h"
#include "gable/platform.h"
#include "gable/temporary.h"

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace {

/// The compiler completes an output path that has no extension with the host's
/// executable suffix, so tests refer to the produced file through this helper.
std::string executablePath(const std::string& path) {
  return path + gable::platform::executableSuffix();
}

struct Run {
  int exitCode;
  std::string out;
  std::string err;
};

Run runCli(const std::vector<std::string>& arguments) {
  std::ostringstream out;
  std::ostringstream err;
  const gable::CliExitCode code = gable::runCommandLine(arguments, out, err);
  return Run{static_cast<int>(code), out.str(), err.str()};
}

/// Writes `text` to a fresh file inside a temporary directory owned by the
/// caller-provided directory object.
std::string writeSource(const gable::TemporaryDirectory& directory, const std::string& name,
                        const std::string& text) {
  const std::string path = directory.file(name);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
  return path;
}

/// Reads a whole file, for asserting what the compiler wrote.
std::string readSource(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

gable::TemporaryDirectory makeDirectory() {
  std::string error;
  auto directory = gable::TemporaryDirectory::create(error);
  if (!directory) {
    throw gabletest::Failure("cannot create temporary directory: " + error);
  }
  return std::move(*directory);
}

}  // namespace

GABLE_TEST(cli_prints_help) {
  const Run run = runCli({"--help"});
  GABLE_CHECK_EQ(0, run.exitCode);
  GABLE_CHECK(run.out.find("Usage:") != std::string::npos);
  GABLE_CHECK(run.out.find("gable build") != std::string::npos);
  GABLE_CHECK(run.out.find("gable run") != std::string::npos);
  GABLE_CHECK(run.out.find("Exit status:") != std::string::npos);
}

GABLE_TEST(cli_no_arguments_shows_help) {
  const Run run = runCli({});
  GABLE_CHECK_EQ(0, run.exitCode);
  GABLE_CHECK(run.out.find("Usage:") != std::string::npos);
}

GABLE_TEST(cli_prints_version) {
  const Run run = runCli({"--version"});
  GABLE_CHECK_EQ(0, run.exitCode);
  GABLE_CHECK(run.out.find("gable ") == 0);
  GABLE_CHECK(run.out.find("language version") != std::string::npos);
}

GABLE_TEST(cli_rejects_unknown_command) {
  const Run run = runCli({"frobnicate"});
  GABLE_CHECK_EQ(2, run.exitCode);
  GABLE_CHECK(run.err.find("unknown command 'frobnicate'") != std::string::npos);
}

GABLE_TEST(cli_rejects_unknown_option) {
  const Run run = runCli({"build", "x.gable", "--turbo"});
  GABLE_CHECK_EQ(2, run.exitCode);
  GABLE_CHECK(run.err.find("unknown option '--turbo'") != std::string::npos);
}

GABLE_TEST(cli_rejects_missing_source_file) {
  const Run run = runCli({"build"});
  GABLE_CHECK_EQ(2, run.exitCode);
  GABLE_CHECK(run.err.find("requires a source file") != std::string::npos);
}

GABLE_TEST(cli_rejects_multiple_source_files) {
  const Run run = runCli({"build", "a.gable", "b.gable"});
  GABLE_CHECK_EQ(2, run.exitCode);
  GABLE_CHECK(run.err.find("only one source file") != std::string::npos);
}

GABLE_TEST(cli_rejects_unsupported_optimization_level) {
  const Run run = runCli({"build", "a.gable", "-O3"});
  GABLE_CHECK_EQ(2, run.exitCode);
  GABLE_CHECK(run.err.find("unsupported optimization level") != std::string::npos);
}

GABLE_TEST(cli_rejects_emit_c_with_run) {
  const Run run = runCli({"run", "a.gable", "--emit-c"});
  GABLE_CHECK_EQ(2, run.exitCode);
  GABLE_CHECK(run.err.find("--emit-c cannot be combined with 'run'") != std::string::npos);
}

GABLE_TEST(cli_rejects_output_option_with_run) {
  const Run run = runCli({"run", "a.gable", "-o", "program"});
  GABLE_CHECK_EQ(2, run.exitCode);
  GABLE_CHECK(run.err.find("'run' does not accept -o") != std::string::npos);
}

GABLE_TEST(cli_reports_missing_input_file) {
  const Run run = runCli({"build", "/nonexistent/path/to/program.gable"});
  GABLE_CHECK_EQ(1, run.exitCode);
  GABLE_CHECK(run.err.find("cannot read '/nonexistent/path/to/program.gable'") != std::string::npos);
}

GABLE_TEST(cli_emit_c_writes_translation_unit_to_stdout) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "ok.gable", gabletest::kSimpleProgram());
  const Run run = runCli({"build", source, "--emit-c"});
  GABLE_CHECK_EQ(0, run.exitCode);
  GABLE_CHECK(run.out.find("#include \"gable/rt.h\"") != std::string::npos);
  GABLE_CHECK(run.out.find("gable_fn_main") != std::string::npos);
  GABLE_CHECK_EQ(std::string(""), run.err);
}

GABLE_TEST(cli_emit_c_writes_translation_unit_to_file) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "ok.gable", gabletest::kSimpleProgram());
  const std::string destination = directory.file("module.c");
  const Run run = runCli({"build", source, "--emit-c=" + destination});
  GABLE_CHECK_EQ(0, run.exitCode);

  std::ifstream file(destination);
  std::stringstream contents;
  contents << file.rdbuf();
  GABLE_CHECK(contents.str().find("gable_fn_main") != std::string::npos);
}

GABLE_TEST(cli_reports_syntax_errors_with_exit_code_one) {
  gable::TemporaryDirectory directory = makeDirectory();
  // Two statements on one line: a line break or ';' has to separate them.
  const std::string source =
      writeSource(directory, "bad.gable", "fn main() -> int {\n  let a = 1 let b = 2\n}\n");
  const Run run = runCli({"build", source});
  GABLE_CHECK_EQ(1, run.exitCode);
  GABLE_CHECK(run.err.find("expected a line break") != std::string::npos);
  GABLE_CHECK(run.err.find("bad.gable:2:") != std::string::npos);
  GABLE_CHECK(run.err.find("error:") != std::string::npos);
}

GABLE_TEST(cli_refuses_to_overwrite_source) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "same.gable", gabletest::kSimpleProgram());
  const Run run = runCli({"build", source, "-o", source});
  GABLE_CHECK_EQ(2, run.exitCode);
  GABLE_CHECK(run.err.find("refusing to write") != std::string::npos);
  // The source must be byte-for-byte unchanged.
  std::ifstream file(source);
  std::stringstream contents;
  contents << file.rdbuf();
  GABLE_CHECK_EQ(std::string(gabletest::kSimpleProgram()), contents.str());
}

GABLE_TEST(cli_refuses_emit_c_onto_the_source) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "emit.gable", gabletest::kSimpleProgram());
  const Run run = runCli({"build", source, "--emit-c=" + source});
  GABLE_CHECK_EQ(2, run.exitCode);
  GABLE_CHECK(run.err.find("refusing to write") != std::string::npos);

  std::ifstream file(source);
  std::stringstream contents;
  contents << file.rdbuf();
  GABLE_CHECK_EQ(std::string(gabletest::kSimpleProgram()), contents.str());
}

GABLE_TEST(cli_refuses_output_through_a_symlink_to_the_source) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "target.gable", gabletest::kSimpleProgram());
  const std::string link = directory.file("alias.gable");
  std::error_code ignored;
  std::filesystem::create_symlink(source, link, ignored);
  if (ignored) {
    return;  // filesystem does not support symlinks; behaviour is covered elsewhere
  }

  const Run emit = runCli({"build", source, "--emit-c=" + link});
  GABLE_CHECK_EQ(2, emit.exitCode);
  GABLE_CHECK(emit.err.find("refusing to write") != std::string::npos);

  std::ifstream file(source);
  std::stringstream contents;
  contents << file.rdbuf();
  GABLE_CHECK_EQ(std::string(gabletest::kSimpleProgram()), contents.str());
}

GABLE_TEST(cli_accepts_output_paths_with_spaces) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "spaces.gable", gabletest::kSimpleProgram());
  const std::string nested = directory.file("dir with spaces");
  std::error_code ignored;
  std::filesystem::create_directory(nested, ignored);
  const std::string output = nested + "/pro gram";
  const Run run = runCli({"build", source, "-o", output});
  GABLE_CHECK_EQ(0, run.exitCode);
  GABLE_CHECK(std::ifstream(executablePath(output)).good());
}

GABLE_TEST(cli_reports_missing_output_directory) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "ok.gable", gabletest::kSimpleProgram());
  const Run run = runCli({"build", source, "-o", directory.file("missing/out")});
  GABLE_CHECK_EQ(2, run.exitCode);
  GABLE_CHECK(run.err.find("does not exist") != std::string::npos);
}

GABLE_TEST(cli_rejects_directory_as_output) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "ok.gable", gabletest::kSimpleProgram());
  std::error_code ignored;
  std::filesystem::create_directory(directory.file("outdir"), ignored);
  const Run run = runCli({"build", source, "-o", directory.file("outdir")});
  GABLE_CHECK_EQ(2, run.exitCode);
  GABLE_CHECK(run.err.find("is a directory") != std::string::npos);
}

GABLE_TEST(cli_keeps_existing_executable_when_build_fails) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "flaky.gable", gabletest::kSimpleProgram());
  const std::string output = directory.file("existing");
  GABLE_CHECK_EQ(0, runCli({"build", source, "-o", output}).exitCode);

  std::ifstream before(executablePath(output), std::ios::binary);
  std::stringstream original;
  original << before.rdbuf();
  before.close();
  GABLE_CHECK(!original.str().empty());

  writeSource(directory, "flaky.gable", "fn main() -> int { return missing; }\n");
  const Run failed = runCli({"build", source, "-o", output});
  GABLE_CHECK_EQ(1, failed.exitCode);

  std::ifstream after(executablePath(output), std::ios::binary);
  std::stringstream preserved;
  preserved << after.rdbuf();
  GABLE_CHECK_EQ(original.str(), preserved.str());
}

GABLE_TEST(cli_leaves_no_temporary_files_behind) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "clean.gable", gabletest::kSimpleProgram());
  GABLE_CHECK_EQ(0, runCli({"build", source, "-o", directory.file("clean")}).exitCode);
  GABLE_CHECK_EQ(0, runCli({"build", source, "--emit-c=" + directory.file("clean.c")}).exitCode);

  std::size_t entries = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory.path())) {
    (void)entry;
    ++entries;
  }
  // source, executable, generated C
  GABLE_CHECK_EQ(std::size_t(3), entries);
}

GABLE_TEST(cli_default_output_path_drops_gable_extension) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "program.gable", gabletest::kSimpleProgram());
  const Run run = runCli({"build", source});
  GABLE_CHECK_EQ(0, run.exitCode);
  GABLE_CHECK(std::ifstream(executablePath(directory.file("program"))).good());
}

GABLE_TEST(cli_builds_executable_at_requested_path) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "program.gable", gabletest::kSimpleProgram());
  const std::string output = directory.file("custom-name");
  const Run run = runCli({"build", source, "-o", output});
  GABLE_CHECK_EQ(0, run.exitCode);
  GABLE_CHECK_EQ(std::string(""), run.err);
  GABLE_CHECK(std::ifstream(executablePath(output)).good());
}

GABLE_TEST(cli_verbose_reports_produced_executable) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "program.gable", gabletest::kSimpleProgram());
  const Run run = runCli({"build", source, "-o", directory.file("verbose"), "-v"});
  GABLE_CHECK_EQ(0, run.exitCode);
  GABLE_CHECK(run.err.find("gable: running:") != std::string::npos);
  GABLE_CHECK(run.out.find("gable: produced ") != std::string::npos);
}

GABLE_TEST(cli_check_accepts_a_valid_program) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "valid.gable", gabletest::kSimpleProgram());
  const Run run = runCli({"check", source});
  GABLE_CHECK_EQ(0, run.exitCode);
  GABLE_CHECK_EQ(std::string(""), run.err);
  GABLE_CHECK_EQ(std::string(""), run.out);
  // 'check' writes no output at all.
  GABLE_CHECK(!std::ifstream(executablePath(directory.file("valid"))).good());
}

GABLE_TEST(cli_check_reports_type_errors) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source =
      writeSource(directory, "invalid.gable", "fn main() -> int { return missing; }\n");
  const Run run = runCli({"check", source});
  GABLE_CHECK_EQ(1, run.exitCode);
  GABLE_CHECK(run.err.find("undefined variable 'missing'") != std::string::npos);
  GABLE_CHECK(run.err.find("1 error(s) in ") != std::string::npos);
}

GABLE_TEST(cli_check_reports_syntax_errors) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source =
      writeSource(directory, "broken.gable", "fn main() -> int {\n  let a = 1 let b = 2\n}\n");
  const Run run = runCli({"check", source});
  GABLE_CHECK_EQ(1, run.exitCode);
  GABLE_CHECK(run.err.find("expected a line break") != std::string::npos);
}

GABLE_TEST(cli_check_rejects_output_options) {
  GABLE_CHECK_EQ(2, runCli({"check", "a.gable", "-o", "out"}).exitCode);
  GABLE_CHECK_EQ(2, runCli({"check", "a.gable", "--emit-c"}).exitCode);
  GABLE_CHECK_EQ(2, runCli({"check", "a.gable", "--", "arg"}).exitCode);
}

GABLE_TEST(cli_run_forwards_arguments_after_double_dash) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(
      directory, "args.gable",
      "fn main() -> int {\n"
      "  if arg_count() != 3 { return 1; }\n"
      "  if arg(1) != \"first\" { return 2; }\n"
      "  if arg(2) != \"second\" { return 3; }\n"
      "  return 0;\n"
      "}\n");
  const Run run = runCli({"run", source, "--", "first", "second"});
  GABLE_CHECK_EQ(0, run.exitCode);
}

GABLE_TEST(cli_run_forwards_the_program_exit_status) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source =
      writeSource(directory, "status.gable", "fn main() -> int { return 7; }\n");
  const Run run = runCli({"run", source});
  GABLE_CHECK_EQ(7, run.exitCode);
}

// A source path is user input. A directory whose name ends with `*` makes the
// path contain `*/`, which used to close the banner comment of the generated
// translation unit and turn the rest of the path into C.
GABLE_TEST(cli_builds_a_program_whose_path_contains_a_comment_terminator) {
  gable::TemporaryDirectory directory = makeDirectory();
  std::error_code ignored;
  const std::string hostile = directory.file("evil*");
  std::filesystem::create_directory(hostile, ignored);
  if (!std::filesystem::is_directory(hostile)) {
    return;  // the filesystem refuses the name; nothing to test here
  }
  const std::string source = writeSource(directory, "evil*/program.gable",
                                         gabletest::kSimpleProgram());
  const std::string output = directory.file("built");

  const Run build = runCli({"build", source, "-o", output});
  GABLE_CHECK_EQ(0, build.exitCode);
  GABLE_CHECK_EQ(std::string(""), build.err);
  GABLE_CHECK(std::ifstream(executablePath(output)).good());

  // The path appears in the banner, escaped, and the unit still compiles.
  const Run emitted = runCli({"build", source, "--emit-c"});
  GABLE_CHECK_EQ(0, emitted.exitCode);
  GABLE_CHECK(emitted.out.find("evil\\x2A/program.gable") != std::string::npos);
  // The banner stays three lines: the first terminator in the whole file is the
  // one that closes it, so nothing from the path escaped into the code.
  const std::size_t terminator = emitted.out.find("*/");
  GABLE_CHECK(terminator != std::string::npos);
  GABLE_CHECK_EQ(std::size_t(2), static_cast<std::size_t>(std::count(
                                     emitted.out.begin(), emitted.out.begin() +
                                                              static_cast<std::ptrdiff_t>(terminator),
                                     '\n')));
}

namespace {

/// Creates `count` symbolic links in `directory` whose names follow the pattern
/// an earlier implementation used for its output temporary file, each pointing at
/// `victim`. Returns false when the file system refuses symlinks.
bool plantTemporaryNameSymlinks(const gable::TemporaryDirectory& directory,
                                const std::string& targetName, const std::string& victim,
                                int count) {
  for (int index = 1; index <= count; ++index) {
    const std::string link =
        directory.file("." + targetName + ".gable-tmp-" +
                       std::to_string(gable::platform::processId()) + "-" + std::to_string(index));
    std::error_code error;
    std::filesystem::create_symlink(victim, link, error);
    if (index == 1 && error) {
      return false;
    }
  }
  return true;
}

std::string readWholeFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::stringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

}  // namespace

// The temporary file the compiler writes before it replaces the destination must
// not be a predictable name in a shared directory: another user could create that
// name first as a symbolic link to a file they cannot otherwise write, and the
// compiler would then write through it. The compiler now creates a private
// staging directory, so none of the planted links are used, and the victim keeps
// its contents.
GABLE_TEST(cli_does_not_follow_temporary_name_symlinks) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "program.gable", gabletest::kSimpleProgram());
  const std::string output = directory.file("built");

  const std::string victim = directory.file("victim.txt");
  {
    std::ofstream file(victim, std::ios::binary | std::ios::trunc);
    file << "precious\n";
  }
  if (!plantTemporaryNameSymlinks(directory, "built", victim, 500)) {
    return;  // no symlink support; the behaviour is covered on hosts that have it
  }
  if (!plantTemporaryNameSymlinks(directory, "module.c", victim, 500)) {
    return;
  }

  for (int build = 0; build < 3; ++build) {
    GABLE_CHECK_EQ(0, runCli({"build", source, "-o", output}).exitCode);
  }
  // `--emit-c=<path>` writes the translation unit through the compiler's own
  // file writer, which is the path that followed a symbolic link.
  const std::string emitted = directory.file("module.c");
  GABLE_CHECK_EQ(0, runCli({"build", source, "--emit-c=" + emitted}).exitCode);

  GABLE_CHECK_EQ(std::string("precious\n"), readWholeFile(victim));
  GABLE_CHECK(std::ifstream(executablePath(output)).good());
  GABLE_CHECK(readWholeFile(emitted).find("gable_fn_main") != std::string::npos);
}

// A regular file that already occupies a plausible temporary name is neither
// truncated nor used: only the destination is written.
GABLE_TEST(cli_leaves_foreign_files_alone) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "program.gable", gabletest::kSimpleProgram());
  const std::string output = directory.file("built");

  std::vector<std::string> foreign;
  for (int index = 1; index <= 20; ++index) {
    const std::string path =
        directory.file("." + std::string("built") + ".gable-tmp-" +
                       std::to_string(gable::platform::processId()) + "-" + std::to_string(index));
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << "keep-" << index << "\n";
    foreign.push_back(path);
  }

  GABLE_CHECK_EQ(0, runCli({"build", source, "-o", output}).exitCode);
  for (std::size_t index = 0; index < foreign.size(); ++index) {
    GABLE_CHECK_EQ("keep-" + std::to_string(index + 1) + "\n", readWholeFile(foreign[index]));
  }
}

// Whatever path the build takes, the staging directory does not survive it.
GABLE_TEST(cli_removes_its_staging_directory) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string good = writeSource(directory, "good.gable", gabletest::kSimpleProgram());
  const std::string bad = writeSource(directory, "bad.gable", "fn main() -> int { return x; }\n");

  GABLE_CHECK_EQ(0, runCli({"build", good, "-o", directory.file("ok")}).exitCode);
  GABLE_CHECK_EQ(1, runCli({"build", bad, "-o", directory.file("failed")}).exitCode);
  GABLE_CHECK_EQ(0, runCli({"build", good, "--emit-c=" + directory.file("emitted.c")}).exitCode);

  std::size_t stagingDirectories = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory.path())) {
    if (entry.path().filename().string().rfind(".gable-", 0) == 0) {
      ++stagingDirectories;
    }
  }
  GABLE_CHECK_EQ(std::size_t(0), stagingDirectories);
}

// Two compilations of the same destination must not see each other's temporary
// file, whichever of them finishes last. The final file is complete: it matches
// one of the two programs byte for byte.
GABLE_TEST(cli_concurrent_writes_to_the_same_output_stay_whole) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string output = directory.file("shared.txt");

  std::vector<std::string> payloads;
  payloads.push_back(std::string(64 * 1024, 'a'));
  payloads.push_back(std::string(64 * 1024, 'b'));

  std::vector<std::thread> writers;
  std::atomic<int> failures{0};
  for (const std::string& payload : payloads) {
    writers.emplace_back([&, payload]() {
      for (int attempt = 0; attempt < 20; ++attempt) {
        std::string error;
        auto file = gable::AtomicOutputFile::create(output, error);
        if (!file) {
          ++failures;
          return;
        }
        if (!file->write(payload, error) || !file->commit(error)) {
          ++failures;
          return;
        }
      }
    });
  }
  for (std::thread& writer : writers) {
    writer.join();
  }

  GABLE_CHECK_EQ(0, failures.load());
  const std::string contents = readWholeFile(output);
  const bool first = contents == payloads[0];
  const bool second = contents == payloads[1];
  GABLE_CHECK(first || second);

  std::size_t stagingDirectories = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory.path())) {
    if (entry.path().filename().string().rfind(".gable-", 0) == 0) {
      ++stagingDirectories;
    }
  }
  GABLE_CHECK_EQ(std::size_t(0), stagingDirectories);
}

namespace {

/// Sets an environment variable for the duration of a scope and restores the
/// previous value afterwards, so one test cannot change another's environment.
class ScopedEnvironmentVariable {
 public:
  ScopedEnvironmentVariable(const char* name, const char* value) : name_(name) {
    const char* previous = std::getenv(name);
    if (previous != nullptr) {
      hadValue_ = true;
      previous_ = previous;
    }
#if defined(_WIN32)
    _putenv_s(name_.c_str(), value);
#else
    ::setenv(name_.c_str(), value, 1);
#endif
  }

  ~ScopedEnvironmentVariable() {
#if defined(_WIN32)
    _putenv_s(name_.c_str(), hadValue_ ? previous_.c_str() : "");
#else
    if (hadValue_) {
      ::setenv(name_.c_str(), previous_.c_str(), 1);
    } else {
      ::unsetenv(name_.c_str());
    }
#endif
  }

  ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
  ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

 private:
  std::string name_;
  std::string previous_;
  bool hadValue_ = false;
};

}  // namespace

// Extra options in GABLE_CC_FLAGS reach the C compiler the backend runs, which is
// what makes it possible to build generated programs with a sanitizer. A flag the
// compiler rejects must be reported as a backend failure rather than hidden.
GABLE_TEST(cli_extra_c_compiler_flags_reach_the_backend) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "flags.gable", gabletest::kSimpleProgram());
  const std::string output = directory.file("flags-program");
#if defined(_WIN32)
  const char* rejectedFlag = "/FIgable-flag-test-missing-header.h";
#else
  const char* rejectedFlag = "-include gable-flag-test-missing-header.h";
#endif
  {
    const ScopedEnvironmentVariable flags("GABLE_CC_FLAGS", rejectedFlag);
    const Run failed = runCli({"build", source, "-o", output});
    // A failing C compiler is an internal failure (exit status 70), not a
    // diagnostic about the source file.
    GABLE_CHECK_EQ(70, failed.exitCode);
    GABLE_CHECK(failed.err.find("the C compiler failed") != std::string::npos);
  }
  const Run succeeded = runCli({"build", source, "-o", output});
  GABLE_CHECK_EQ(0, succeeded.exitCode);
}

#if !defined(_WIN32)
// The C compiler prints progress chatter (cl.exe prints the name of each source
// file) on standard output. That is not a diagnostic, so a successful build must
// not forward it: `gable build` writing to standard error on success is a bug that
// only showed up on Windows, where the chatter comes from cl.exe.
GABLE_TEST(cli_does_not_forward_c_compiler_stdout_on_success) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "quiet.gable", gabletest::kSimpleProgram());
  const std::string output = directory.file("quiet-program");
  const std::string wrapper = directory.file("chatty-cc.sh");
  {
    std::ofstream script(wrapper, std::ios::binary | std::ios::trunc);
    script << "#!/bin/sh\n"
              "printf 'chatty compiler banner\\n'\n"
              "exec \"$GABLE_TEST_REAL_CC\" \"$@\"\n";
  }
  std::error_code ignored;
  std::filesystem::permissions(wrapper,
                               std::filesystem::perms::owner_exec |
                                   std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::add, ignored);

  const ScopedEnvironmentVariable compiler("GABLE_CC", wrapper.c_str());
  const ScopedEnvironmentVariable realCompiler("GABLE_TEST_REAL_CC", "cc");
  const Run run = runCli({"build", source, "-o", output});
  GABLE_CHECK_EQ(0, run.exitCode);
  GABLE_CHECK_EQ(std::string(""), run.err);
  GABLE_CHECK(std::filesystem::exists(output + gable::platform::executableSuffix()));

  // A verbose run does forward it, so the user can see what was run and what it
  // printed.
  const Run verbose = runCli({"build", source, "-o", output, "-v"});
  GABLE_CHECK_EQ(0, verbose.exitCode);
  GABLE_CHECK(verbose.err.find("chatty compiler banner") != std::string::npos);
}
#endif

// A name collision has to be distinguishable from any other failure without
// reading the message: the message comes from the operating system and is
// localised, and the staging-directory retry must not depend on its wording.
GABLE_TEST(output_reports_a_directory_collision_by_value) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string path = directory.file("staging");
  std::string error;

  GABLE_CHECK(gable::platform::createDirectoryExclusive(path, error) ==
              gable::platform::CreateDirectoryResult::Created);
  GABLE_CHECK(error.empty());

  error.clear();
  GABLE_CHECK(gable::platform::createDirectoryExclusive(path, error) ==
              gable::platform::CreateDirectoryResult::AlreadyExists);
  GABLE_CHECK(!error.empty());

  // A path whose parent does not exist cannot be created, and that is not a
  // collision, so the caller must stop retrying instead of spinning.
  error.clear();
  const std::string missingParent = directory.file("no-such-directory/staging");
  GABLE_CHECK(gable::platform::createDirectoryExclusive(missingParent, error) ==
              gable::platform::CreateDirectoryResult::Failed);
  GABLE_CHECK(!error.empty());
}

GABLE_TEST(fmt_rewrites_a_file_and_check_reports_it) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string path =
      writeSource(directory, "messy.gable", "fn main() {\n      print(\"hi\")\n   }\n");

  // `--check` writes nothing and reports the file through the exit status, so a
  // CI job can require that every file is formatted without touching the tree.
  const Run check = runCli({"fmt", "--check", path});
  GABLE_CHECK_EQ(1, check.exitCode);
  GABLE_CHECK(check.out.find("would be reformatted") != std::string::npos);
  GABLE_CHECK(readSource(path) == "fn main() {\n      print(\"hi\")\n   }\n");

  const Run format = runCli({"fmt", path});
  GABLE_CHECK_EQ(0, format.exitCode);
  GABLE_CHECK(readSource(path) == "fn main() {\n    print(\"hi\")\n}\n");

  // Formatting an already formatted file changes nothing and reports success.
  const Run again = runCli({"fmt", "--check", path});
  GABLE_CHECK_EQ(0, again.exitCode);
}

GABLE_TEST(fmt_refuses_a_file_with_a_syntax_error) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string text = "fn main() {\n    print(\n}\n";
  const std::string path = writeSource(directory, "broken.gable", text);

  const Run run = runCli({"fmt", path});
  GABLE_CHECK_EQ(1, run.exitCode);
  GABLE_CHECK(run.err.find("error") != std::string::npos);
  // A file that does not parse is never rewritten, whatever is wrong with it.
  GABLE_CHECK(readSource(path) == text);
}

GABLE_TEST(fmt_requires_a_file) {
  const Run run = runCli({"fmt"});
  GABLE_CHECK_EQ(2, run.exitCode);
  GABLE_CHECK(run.err.find("requires at least one source file") != std::string::npos);
}

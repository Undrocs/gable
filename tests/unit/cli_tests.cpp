#include "test_framework.h"
#include "test_helpers.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gable/cli.h"
#include "gable/temporary.h"

namespace {

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
  GABLE_CHECK(run.err.find("cannot open source file") != std::string::npos);
}

GABLE_TEST(cli_emit_c_writes_translation_unit_to_stdout) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "ok.gable", gabletest::kSimpleProgram());
  const Run run = runCli({"build", source, "--emit-c"});
  GABLE_CHECK_EQ(0, run.exitCode);
  GABLE_CHECK(run.out.find("#include <gable/rt.h>") != std::string::npos);
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
  const std::string source = writeSource(directory, "bad.gable", "fn main() -> i32 { return 0 }\n");
  const Run run = runCli({"build", source});
  GABLE_CHECK_EQ(1, run.exitCode);
  GABLE_CHECK(run.err.find("expected ';'") != std::string::npos);
  GABLE_CHECK(run.err.find("bad.gable:1:") != std::string::npos);
  GABLE_CHECK(run.err.find("error:") != std::string::npos);
}

GABLE_TEST(cli_refuses_to_overwrite_source) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "same.gable", gabletest::kSimpleProgram());
  const Run run = runCli({"build", source, "-o", source});
  GABLE_CHECK_EQ(2, run.exitCode);
  GABLE_CHECK(run.err.find("refusing to overwrite the source file") != std::string::npos);
}

GABLE_TEST(cli_default_output_path_drops_gable_extension) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "program.gable", gabletest::kSimpleProgram());
  const Run run = runCli({"build", source});
  GABLE_CHECK_EQ(0, run.exitCode);
  GABLE_CHECK(std::ifstream(directory.file("program")).good());
}

GABLE_TEST(cli_builds_executable_at_requested_path) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "program.gable", gabletest::kSimpleProgram());
  const std::string output = directory.file("custom-name");
  const Run run = runCli({"build", source, "-o", output});
  GABLE_CHECK_EQ(0, run.exitCode);
  GABLE_CHECK_EQ(std::string(""), run.err);
  GABLE_CHECK(std::ifstream(output).good());
}

GABLE_TEST(cli_verbose_reports_produced_executable) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "program.gable", gabletest::kSimpleProgram());
  const Run run = runCli({"build", source, "-o", directory.file("verbose"), "-v"});
  GABLE_CHECK_EQ(0, run.exitCode);
  GABLE_CHECK(run.err.find("gable: running:") != std::string::npos);
  GABLE_CHECK(run.out.find("gable: produced ") != std::string::npos);
}

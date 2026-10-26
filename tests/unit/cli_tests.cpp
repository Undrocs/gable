#include "test_framework.h"
#include "test_helpers.h"

#include <filesystem>
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
  GABLE_CHECK(std::ifstream(output).good());
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

  std::ifstream before(output, std::ios::binary);
  std::stringstream original;
  original << before.rdbuf();
  before.close();
  GABLE_CHECK(!original.str().empty());

  writeSource(directory, "flaky.gable", "fn main() -> i32 { return missing; }\n");
  const Run failed = runCli({"build", source, "-o", output});
  GABLE_CHECK_EQ(1, failed.exitCode);

  std::ifstream after(output, std::ios::binary);
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

GABLE_TEST(cli_check_accepts_a_valid_program) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source = writeSource(directory, "valid.gable", gabletest::kSimpleProgram());
  const Run run = runCli({"check", source});
  GABLE_CHECK_EQ(0, run.exitCode);
  GABLE_CHECK_EQ(std::string(""), run.err);
  GABLE_CHECK_EQ(std::string(""), run.out);
  // 'check' writes no output at all.
  GABLE_CHECK(!std::ifstream(directory.file("valid")).good());
}

GABLE_TEST(cli_check_reports_type_errors) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source =
      writeSource(directory, "invalid.gable", "fn main() -> i32 { return missing; }\n");
  const Run run = runCli({"check", source});
  GABLE_CHECK_EQ(1, run.exitCode);
  GABLE_CHECK(run.err.find("undefined variable 'missing'") != std::string::npos);
  GABLE_CHECK(run.err.find("1 error(s) in ") != std::string::npos);
}

GABLE_TEST(cli_check_reports_syntax_errors) {
  gable::TemporaryDirectory directory = makeDirectory();
  const std::string source =
      writeSource(directory, "broken.gable", "fn main() -> i32 { return 0 }\n");
  const Run run = runCli({"check", source});
  GABLE_CHECK_EQ(1, run.exitCode);
  GABLE_CHECK(run.err.find("expected ';'") != std::string::npos);
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
      "fn main() -> i32 {\n"
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
      writeSource(directory, "status.gable", "fn main() -> i32 { return 7; }\n");
  const Run run = runCli({"run", source});
  GABLE_CHECK_EQ(7, run.exitCode);
}

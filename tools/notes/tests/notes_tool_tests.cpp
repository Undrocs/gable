// Acceptance tests for the notes tool.
//
// The tool is written in Gable, so these tests build it with the compiler under
// test and then drive the resulting executable exactly as a user would: through
// its command line and its file. They are the v0.2 acceptance test for a real
// multi-file program that uses arguments, strings, structured data, and file
// I/O.
//
// Usage: notes_tool_tests <path-to-gable> <path-to-notes.gable>
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

int failures = 0;
int checks = 0;

void report(const std::string& name, const std::string& message) {
  ++failures;
  std::cerr << "FAIL " << name << "\n    " << message << "\n";
}

void check(bool condition, const std::string& name, const std::string& message) {
  ++checks;
  if (!condition) {
    report(name, message);
  }
}

void checkEqual(const std::string& actual, const std::string& expected, const std::string& name,
                const char* label) {
  ++checks;
  if (actual != expected) {
    report(name, std::string(label) + " mismatch\n    expected: " + expected + "\n    actual: " +
                     actual);
  }
}

void checkEqual(int actual, int expected, const std::string& name, const char* label) {
  ++checks;
  if (actual != expected) {
    report(name, std::string(label) + " mismatch\n    expected: " +
                     std::to_string(expected) + "\n    actual: " + std::to_string(actual));
  }
}

std::string readFile(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

struct Run {
  int exitCode = -1;
  std::string out;
  std::string err;
  bool started = false;
};

class Tool {
 public:
  Tool(std::string executable, std::string notesFile)
      : executable_(std::move(executable)), notesFile_(std::move(notesFile)) {}

  Run run(const std::vector<std::string>& arguments) const {
    std::vector<std::string> command{executable_, "--file", notesFile_};
    command.insert(command.end(), arguments.begin(), arguments.end());
    const gable::ProcessResult result = gable::runProcessCapturing(command);
    Run run;
    run.started = result.started;
    run.exitCode = result.exitCode;
    run.out = result.standardOutput;
    run.err = result.standardError;
    return run;
  }

  std::string stored() const { return readFile(notesFile_); }

 private:
  std::string executable_;
  std::string notesFile_;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: notes_tool_tests <path-to-gable> <path-to-notes.gable>\n";
    return 2;
  }
  const std::string compiler = argv[1];
  const std::string source = argv[2];

  std::string temporaryError;
  auto scratch = gable::TemporaryDirectory::create(temporaryError);
  if (!scratch) {
    std::cerr << "cannot create a temporary directory: " << temporaryError << "\n";
    return 2;
  }
  const std::string executable =
      scratch->file("notes" + std::string(gable::platform::executableSuffix()));
  const std::string notesFile = scratch->file("notes.txt");

  const gable::ProcessResult build = gable::runProcessCapturing(
      {compiler, "build", source, "-o", executable});
  check(build.started && build.exitCode == 0, "build",
        "building the tool failed" + (build.standardError.empty()
                                          ? std::string()
                                          : ": " + build.standardError));
  if (build.exitCode != 0) {
    std::cout << "FAILED 0/" << checks << " notes tool tests\n";
    return 1;
  }

  const Tool tool(executable, notesFile);

  // An empty list is not an error and the file is not created by reading it.
  {
    const Run list = tool.run({"list"});
    checkEqual(list.out, "no tasks\n", "list_empty", "stdout");
    checkEqual(list.exitCode, 0, "list_empty", "exit status");
    check(!fs::exists(notesFile), "list_empty", "listing must not create the notes file");
  }

  // Adding tasks appends to the file and reports what happened.
  {
    const Run first = tool.run({"add", "buy", "milk"});
    checkEqual(first.out, "added: buy milk\n", "add_first", "stdout");
    checkEqual(first.exitCode, 0, "add_first", "exit status");
    checkEqual(tool.stored(), "0\tbuy milk\n", "add_first", "file contents");

    const Run second = tool.run({"add", "write the release notes"});
    checkEqual(second.exitCode, 0, "add_second", "exit status");
    checkEqual(tool.stored(), "0\tbuy milk\n0\twrite the release notes\n", "add_second",
               "file contents");
  }

  // Listing shows stored tasks with one-based numbers and a status marker.
  {
    const Run list = tool.run({"list"});
    checkEqual(list.out, "1. [ ] buy milk\n2. [ ] write the release notes\n", "list", "stdout");
    checkEqual(list.exitCode, 0, "list", "exit status");
  }

  // Marking a task done rewrites only that task.
  {
    const Run done = tool.run({"done", "2"});
    checkEqual(done.exitCode, 0, "done", "exit status");
    checkEqual(tool.stored(), "0\tbuy milk\n1\twrite the release notes\n", "done",
               "file contents");

    const Run list = tool.run({"list"});
    checkEqual(list.out, "1. [ ] buy milk\n2. [x] write the release notes\n", "done", "list");

    const Run again = tool.run({"done", "2"});
    checkEqual(again.exitCode, 0, "done_twice", "exit status");
    checkEqual(tool.stored(), "0\tbuy milk\n1\twrite the release notes\n", "done_twice",
               "file contents");
  }

  // Removing a task deletes exactly one line.
  {
    const Run removed = tool.run({"remove", "1"});
    checkEqual(removed.exitCode, 0, "remove", "exit status");
    checkEqual(tool.stored(), "1\twrite the release notes\n", "remove", "file contents");
    const Run list = tool.run({"list"});
    checkEqual(list.out, "1. [x] write the release notes\n", "remove", "list");
  }

  // Out-of-range and malformed numbers are reported, not stored.
  {
    const Run missing = tool.run({"done", "9"});
    checkEqual(missing.exitCode, 3, "missing_task", "exit status");
    checkEqual(missing.err, "notes: no task numbered 9\n", "missing_task", "stderr");

    const Run zero = tool.run({"remove", "0"});
    checkEqual(zero.exitCode, 1, "zero_index", "exit status");

    const Run text = tool.run({"done", "abc"});
    checkEqual(text.exitCode, 1, "text_index", "exit status");
    checkEqual(tool.stored(), "1\twrite the release notes\n", "text_index", "file contents");
  }

  // Titles cannot break the file format.
  {
    const Run tab = tool.run({"add", std::string("bad\ttitle")});
    checkEqual(tab.exitCode, 1, "tab_title", "exit status");
    const Run empty = tool.run({"add"});
    checkEqual(empty.exitCode, 1, "empty_title", "exit status");
    checkEqual(tool.stored(), "1\twrite the release notes\n", "rejected_titles", "file contents");
  }

  // Unknown commands and missing arguments print usage and fail.
  {
    const Run unknown = tool.run({"frobnicate"});
    checkEqual(unknown.exitCode, 1, "unknown_command", "exit status");
    check(unknown.err.find("unknown command 'frobnicate'") != std::string::npos,
          "unknown_command", "stderr does not name the command");
    check(unknown.err.find("usage: notes") != std::string::npos, "unknown_command",
          "stderr does not contain usage");

    const Run none = tool.run({});
    checkEqual(none.exitCode, 1, "no_command", "exit status");

    const Run help = tool.run({"--help"});
    checkEqual(help.exitCode, 0, "help", "exit status");
    check(help.err.find("usage: notes") != std::string::npos, "help",
          "help text is missing from stderr");
  }

  // A malformed file is reported instead of silently discarded.
  {
    std::ofstream file(notesFile, std::ios::binary | std::ios::trunc);
    file << "not a stored task\n";
    file.close();

    const Run list = tool.run({"list"});
    checkEqual(list.exitCode, 2, "corrupt_file", "exit status");
    check(list.err.find("cannot read the notes file") != std::string::npos, "corrupt_file",
          "stderr does not explain the problem");
    checkEqual(tool.stored(), "not a stored task\n", "corrupt_file",
               "corrupt input must not be overwritten");
  }

  // Clearing empties the file.
  {
    std::ofstream file(notesFile, std::ios::binary | std::ios::trunc);
    file << "0\tone\n1\ttwo\n";
    file.close();

    const Run cleared = tool.run({"clear"});
    checkEqual(cleared.exitCode, 0, "clear", "exit status");
    checkEqual(tool.stored(), "", "clear", "file contents");
    const Run list = tool.run({"list"});
    checkEqual(list.out, "no tasks\n", "clear", "list after clear");
  }

  // An unreadable file reports a storage error rather than a crash.
  {
    const std::string directoryAsFile = scratch->file("a-directory");
    std::error_code ignored;
    fs::create_directory(directoryAsFile, ignored);
    const gable::ProcessResult result = gable::runProcessCapturing(
        {executable, "--file", directoryAsFile, "list"});
    check(result.started && result.exitCode == 2, "storage_error",
          "expected exit status 2 for an unusable path, got " +
              std::to_string(result.exitCode));
  }

  const int total = checks;
  std::cout << (failures == 0 ? "PASS " : "FAILED ") << (total - failures) << "/" << total
            << " notes tool tests\n";
  return failures == 0 ? 0 : 1;
}

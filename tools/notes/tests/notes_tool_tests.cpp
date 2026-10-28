// Acceptance tests for the notes tool.
//
// The tool is written in Gable, so these tests build it with the compiler under
// test and then drive the resulting executable exactly as a user would: through
// its command line and its file. They are the v0.2 acceptance test for a real
// multi-file program that uses arguments, strings, structured data, and file
// I/O.
//
// Usage: notes_tool_tests <path-to-gable> <path-to-notes.gable>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "gable/platform.h"
#include "gable/process.h"
#include "gable/temporary.h"

namespace fs = std::filesystem;

namespace {

int failures = 0;
int checks = 0;
int skipped = 0;

/// Records a check that cannot run here, so an unexercised case is visible in
/// the output instead of silently passing.
void skip(const std::string& name, const std::string& why) {
  ++skipped;
  std::cerr << "SKIP " << name << ": " << why << "\n";
}

bool runningAsRoot() {
#if defined(_WIN32)
  return false;
#else
  return ::geteuid() == 0;
#endif
}

/// True when the tool left a temporary file behind next to `path`.
bool hasTemporaryFilesFor(const fs::path& path) {
  const std::string prefix = path.filename().string() + ".tmp-";
  std::error_code ignored;
  for (const auto& entry : fs::directory_iterator(path.parent_path(), ignored)) {
    if (entry.path().filename().string().rfind(prefix, 0) == 0) {
      return true;
    }
  }
  return false;
}

void writeFile(const fs::path& path, const std::string& contents) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << contents;
}

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

  // A stored line with a flag other than "0" or "1", a missing tab, or an empty
  // title is corruption: it is reported and never rewritten.
  {
    const std::vector<std::string> malformed = {
        "x\tbuy milk\n", "2\tbuy milk\n", "0 buy milk\n", "1\t\n", "\tbuy milk\n"};
    for (const std::string& corrupt : malformed) {
      writeFile(notesFile, corrupt);
      const Run list = tool.run({"list"});
      checkEqual(list.exitCode, 2, "malformed_line", "exit status for a malformed line");
      check(list.err.find("cannot read the notes file") != std::string::npos, "malformed_line",
            "stderr does not explain the problem");
      const Run add = tool.run({"add", "another"});
      checkEqual(add.exitCode, 2, "malformed_line_add", "exit status");
      checkEqual(tool.stored(), corrupt, "malformed_line_add",
                 "a malformed line must not be overwritten");
    }
  }

  // A missing file is an empty list for every command, and `add` creates it.
  {
    fs::remove(notesFile);
    const Run list = tool.run({"list"});
    checkEqual(list.exitCode, 0, "missing_file_list", "exit status");
    checkEqual(list.out, "no tasks\n", "missing_file_list", "stdout");
    check(!fs::exists(notesFile), "missing_file_list", "listing must not create the file");

    const Run add = tool.run({"add", "created on demand"});
    checkEqual(add.exitCode, 0, "missing_file_add", "exit status");
    checkEqual(tool.stored(), "0\tcreated on demand\n", "missing_file_add", "file contents");
  }

  // Saving replaces the file instead of rewriting it in place, so no temporary
  // file is left behind and the previous contents survive a failed save.
  {
    writeFile(notesFile, "0\tkept\n");
    const Run add = tool.run({"add", "second"});
    checkEqual(add.exitCode, 0, "atomic_save", "exit status");
    checkEqual(tool.stored(), "0\tkept\n0\tsecond\n", "atomic_save", "file contents");
    check(!hasTemporaryFilesFor(notesFile), "atomic_save", "a temporary file was left behind");

    // A stale temporary file from a killed process must not confuse the next save.
    const std::string stale = notesFile + ".tmp-1-1";
    writeFile(stale, "half-written");
    const Run again = tool.run({"add", "third"});
    checkEqual(again.exitCode, 0, "stale_temporary_file", "exit status");
    checkEqual(tool.stored(), "0\tkept\n0\tsecond\n0\tthird\n", "stale_temporary_file",
               "file contents");
    checkEqual(readFile(stale), "half-written", "stale_temporary_file",
               "a foreign temporary file must not be modified");
    fs::remove(stale);
  }

  // A locked file is reported as busy rather than silently racing the other
  // process.
  {
    const std::string lockFile = notesFile + ".lock";
    writeFile(lockFile, "1\n");
    const Run blocked = tool.run({"add", "blocked"});
    checkEqual(blocked.exitCode, 2, "busy_lock", "exit status");
    check(blocked.err.find("another notes command") != std::string::npos, "busy_lock",
          "stderr does not explain that the file is in use");

    // A lock older than ten seconds is treated as abandoned: the process that
    // created it died without releasing it.
    fs::last_write_time(lockFile,
                        fs::file_time_type::clock::now() - std::chrono::seconds(30));
    const Run reclaimed = tool.run({"add", "after stale lock"});
    checkEqual(reclaimed.exitCode, 0, "stale_lock", "exit status");
    check(!fs::exists(lockFile), "stale_lock", "the lock was not released");
    check(tool.stored().find("after stale lock") != std::string::npos, "stale_lock",
          "the task was not stored");
  }

  // A read-only file is reported with the permission code and left untouched.
  {
    writeFile(notesFile, "0\tprotected\n");
    std::error_code ignored;
    fs::permissions(notesFile, fs::perms::owner_read, fs::perm_options::replace, ignored);
    if (runningAsRoot()) {
      skip("permission_denied", "tests run as root, so file permissions are not enforced");
    } else {
      const Run add = tool.run({"add", "not allowed"});
      checkEqual(add.exitCode, 2, "permission_denied", "exit status");
      check(add.err.find("code 2") != std::string::npos, "permission_denied",
            "stderr does not report the permission code");
      checkEqual(tool.stored(), "0\tprotected\n", "permission_denied",
                 "the file must be unchanged");
    }
    fs::permissions(notesFile, fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ignored);
  }

  // Two commands that add a task at the same time must both survive: the lock
  // makes each read-modify-write sequence exclusive.
  {
    writeFile(notesFile, "");
    constexpr int kWorkers = 6;
    std::vector<Run> results(kWorkers);
    std::vector<std::thread> workers;
    workers.reserve(kWorkers);
    for (int worker = 0; worker < kWorkers; ++worker) {
      workers.emplace_back([&tool, &results, worker]() {
        results[static_cast<size_t>(worker)] =
            tool.run({"add", "concurrent " + std::to_string(worker)});
      });
    }
    for (std::thread& worker : workers) {
      worker.join();
    }
    int failedRuns = 0;
    std::string detail;
    for (std::size_t index = 0; index < results.size(); ++index) {
      const Run& result = results[index];
      if (!result.started || result.exitCode != 0) {
        ++failedRuns;
        detail += "\n    run " + std::to_string(index) + " exited " +
                  std::to_string(result.exitCode) + ": " + result.err;
      }
    }
    check(failedRuns == 0, "concurrent_add",
          "commands that failed to add a task: " + std::to_string(failedRuns) + detail);
    const std::string stored = tool.stored();
    int missing = 0;
    for (int worker = 0; worker < kWorkers; ++worker) {
      if (stored.find("concurrent " + std::to_string(worker)) == std::string::npos) {
        ++missing;
      }
    }
    checkEqual(missing, 0, "concurrent_add", "tasks lost to a concurrent update");
    check(!hasTemporaryFilesFor(notesFile), "concurrent_add",
          "a temporary file was left behind");
    check(!fs::exists(notesFile + ".lock"), "concurrent_add", "the lock was not released");
  }

  const int total = checks;
  std::cout << (failures == 0 ? "PASS " : "FAILED ") << (total - failures) << "/" << total
            << " notes tool tests";
  if (skipped > 0) {
    std::cout << " (" << skipped << " skipped)";
  }
  std::cout << "\n";
  return failures == 0 ? 0 : 1;
}

// Gable compiler -- structured child process execution, Windows implementation.
//
// CreateProcessW takes a single command-line string, so arguments are quoted
// with the rules the Microsoft C runtime uses to split them again. Captured
// output goes to temporary files rather than pipes: that removes the deadlock
// that would otherwise occur when a child fills the pipe the parent is not
// reading yet, and this driver only needs the complete output of a short-lived
// tool.
#if !defined(_WIN32)
#error "process_windows.cpp must only be compiled on Windows"
#endif

#include "gable/process.h"

#include <windows.h>

#include <string>
#include <vector>

#include "gable/platform.h"

namespace gable {
namespace {

std::string lastErrorMessage(DWORD code) {
  LPSTR buffer = nullptr;
  const DWORD length = ::FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
  std::string message;
  if (length != 0 && buffer != nullptr) {
    message.assign(buffer, length);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
      message.pop_back();
    }
  }
  if (buffer != nullptr) {
    ::LocalFree(buffer);
  }
  if (message.empty()) {
    message = "Windows error " + std::to_string(code);
  }
  return message;
}

std::wstring widen(const std::string& utf8) {
  if (utf8.empty()) {
    return {};
  }
  const int length = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
  if (length <= 0) {
    return {};
  }
  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(),
                        length);
  return wide;
}

/// Quotes one argument so that the child's CRT reconstructs it exactly.
/// Backslashes are only special immediately before a quote or at the end of the
/// argument; those are doubled, and embedded quotes are escaped with a backslash.
std::wstring quoteArgument(const std::wstring& argument) {
  if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    return argument;
  }
  std::wstring result = L"\"";
  for (std::size_t i = 0; i < argument.size(); ++i) {
    std::size_t backslashes = 0;
    while (i < argument.size() && argument[i] == L'\\') {
      ++backslashes;
      ++i;
    }
    if (i == argument.size()) {
      result.append(backslashes * 2, L'\\');
      break;
    }
    if (argument[i] == L'"') {
      result.append(backslashes * 2 + 1, L'\\');
      result.push_back(L'"');
    } else {
      result.append(backslashes, L'\\');
      result.push_back(argument[i]);
    }
  }
  result.push_back(L'"');
  return result;
}

std::wstring buildCommandLine(const std::vector<std::string>& argv) {
  std::wstring commandLine;
  for (std::size_t i = 0; i < argv.size(); ++i) {
    if (i != 0) {
      commandLine.push_back(L' ');
    }
    commandLine += quoteArgument(widen(argv[i]));
  }
  return commandLine;
}

/// A temporary file used to capture one output stream of a child process.
class CaptureFile {
 public:
  CaptureFile() = default;
  CaptureFile(const CaptureFile&) = delete;
  CaptureFile& operator=(const CaptureFile&) = delete;

  ~CaptureFile() {
    if (handle_ != INVALID_HANDLE_VALUE) {
      ::CloseHandle(handle_);
    }
    if (!path_.empty()) {
      ::DeleteFileW(path_.c_str());
    }
  }

  bool create(std::string& error) {
    wchar_t directory[MAX_PATH];
    const DWORD length = ::GetTempPathW(MAX_PATH, directory);
    if (length == 0 || length >= MAX_PATH) {
      error = "cannot locate the temporary directory";
      return false;
    }
    wchar_t path[MAX_PATH];
    if (::GetTempFileNameW(directory, L"gbl", 0, path) == 0) {
      error = lastErrorMessage(::GetLastError());
      return false;
    }
    path_ = path;

    SECURITY_ATTRIBUTES attributes;
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = nullptr;
    attributes.bInheritHandle = TRUE;

    handle_ = ::CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
      error = lastErrorMessage(::GetLastError());
      return false;
    }
    return true;
  }

  HANDLE handle() const { return handle_; }

  std::string readContents() const {
    std::string contents;
    if (handle_ == INVALID_HANDLE_VALUE) {
      return contents;
    }
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    if (::SetFilePointerEx(handle_, zero, nullptr, FILE_BEGIN) == 0) {
      return contents;
    }
    char buffer[8192];
    for (;;) {
      DWORD read = 0;
      if (::ReadFile(handle_, buffer, sizeof(buffer), &read, nullptr) == 0 || read == 0) {
        break;
      }
      contents.append(buffer, read);
    }
    return contents;
  }

 private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
  std::wstring path_;
};

int runWithStdio(const std::vector<std::string>& argv, const std::wstring& commandLine,
                 HANDLE standardOutput, HANDLE standardError, bool capture,
                 ProcessResult* result) {
  STARTUPINFOW startup;
  ::ZeroMemory(&startup, sizeof(startup));
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = standardOutput;
  startup.hStdError = standardError;

  PROCESS_INFORMATION process;
  ::ZeroMemory(&process, sizeof(process));

  // CreateProcessW may modify the command-line buffer, so it must be writable.
  std::wstring mutableCommandLine = commandLine;
  const BOOL created = ::CreateProcessW(
      nullptr, mutableCommandLine.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup,
      &process);
  if (created == 0) {
    const std::string message = lastErrorMessage(::GetLastError());
    if (result != nullptr) {
      result->error = "failed to execute '" + (argv.empty() ? std::string() : argv[0]) +
                      "': " + message;
    }
    return -1;
  }

  ::CloseHandle(process.hThread);
  ::WaitForSingleObject(process.hProcess, INFINITE);

  DWORD exitCode = 0;
  const BOOL obtained = ::GetExitCodeProcess(process.hProcess, &exitCode);
  ::CloseHandle(process.hProcess);
  if (obtained == 0) {
    if (result != nullptr) {
      result->error = "the child process did not report an exit status";
    }
    return -1;
  }
  if (result != nullptr) {
    result->started = true;
    result->exitedNormally = true;
    result->exitCode = static_cast<int>(exitCode);
    (void)capture;
  }
  return static_cast<int>(exitCode);
}

}  // namespace

ProcessResult runProcessCapturing(const std::vector<std::string>& argv) {
  ProcessResult result;
  if (argv.empty()) {
    result.error = "empty command";
    return result;
  }

  CaptureFile standardOutput;
  CaptureFile standardError;
  std::string error;
  if (!standardOutput.create(error)) {
    result.error = "cannot capture standard output: " + error;
    return result;
  }
  if (!standardError.create(error)) {
    result.error = "cannot capture standard error: " + error;
    return result;
  }

  const std::wstring commandLine = buildCommandLine(argv);
  const int exitCode = runWithStdio(argv, commandLine, standardOutput.handle(),
                                    standardError.handle(), true, &result);
  if (exitCode < 0) {
    return result;
  }

  result.standardOutput = standardOutput.readContents();
  result.standardError = standardError.readContents();
  result.exitCode = exitCode;
  return result;
}

int runProcessInheriting(const std::vector<std::string>& argv, std::string& error) {
  if (argv.empty()) {
    error = "empty command";
    return -1;
  }
  const std::wstring commandLine = buildCommandLine(argv);
  ProcessResult result;
  const int exitCode =
      runWithStdio(argv, commandLine, ::GetStdHandle(STD_OUTPUT_HANDLE),
                   ::GetStdHandle(STD_ERROR_HANDLE), false, &result);
  if (exitCode < 0) {
    error = result.error;
  }
  return exitCode;
}

}  // namespace gable

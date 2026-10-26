#include "gable/process.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#if !defined(__unix__) && !defined(__APPLE__)
#error "The Gable compiler currently supports POSIX hosts only (Linux, macOS)."
#endif

#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace gable {
namespace {

std::vector<char*> buildArgv(const std::vector<std::string>& argv) {
  std::vector<char*> raw;
  raw.reserve(argv.size() + 1);
  for (const std::string& argument : argv) {
    raw.push_back(const_cast<char*>(argument.c_str()));
  }
  raw.push_back(nullptr);
  return raw;
}

void reportExecFailure(const std::vector<std::string>& argv, int errorNumber) {
  std::string message = "gable: failed to execute '" + (argv.empty() ? std::string() : argv[0]) +
                        "': " + std::strerror(errorNumber) + "\n";
  const ssize_t ignored = ::write(STDERR_FILENO, message.data(), message.size());
  (void)ignored;
}

bool setNonBlocking(int fd) {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

/// Appends everything currently readable. Returns false at end of file.
bool readAvailable(int fd, std::string& out) {
  char buffer[8192];
  for (;;) {
    const ssize_t count = ::read(fd, buffer, sizeof(buffer));
    if (count > 0) {
      out.append(buffer, static_cast<std::size_t>(count));
      continue;
    }
    if (count == 0) {
      return false;
    }
    if (errno == EINTR) {
      continue;
    }
    return errno == EAGAIN || errno == EWOULDBLOCK;
  }
}

}  // namespace

ProcessResult runProcessCapturing(const std::vector<std::string>& argv) {
  ProcessResult result;
  if (argv.empty()) {
    result.error = "empty command";
    return result;
  }

  int stdoutPipe[2] = {-1, -1};
  int stderrPipe[2] = {-1, -1};
  if (::pipe(stdoutPipe) != 0) {
    result.error = std::string("pipe failed: ") + std::strerror(errno);
    return result;
  }
  if (::pipe(stderrPipe) != 0) {
    result.error = std::string("pipe failed: ") + std::strerror(errno);
    ::close(stdoutPipe[0]);
    ::close(stdoutPipe[1]);
    return result;
  }

  std::vector<char*> raw = buildArgv(argv);
  const pid_t pid = ::fork();
  if (pid < 0) {
    result.error = std::string("fork failed: ") + std::strerror(errno);
    ::close(stdoutPipe[0]);
    ::close(stdoutPipe[1]);
    ::close(stderrPipe[0]);
    ::close(stderrPipe[1]);
    return result;
  }

  if (pid == 0) {
    // Child.
    ::close(stdoutPipe[0]);
    ::close(stderrPipe[0]);
    if (::dup2(stdoutPipe[1], STDOUT_FILENO) < 0 || ::dup2(stderrPipe[1], STDERR_FILENO) < 0) {
      _exit(127);
    }
    ::close(stdoutPipe[1]);
    ::close(stderrPipe[1]);
    ::execvp(raw[0], raw.data());
    reportExecFailure(argv, errno);
    _exit(127);
  }

  // Parent.
  ::close(stdoutPipe[1]);
  ::close(stderrPipe[1]);
  result.started = true;

  // Non-blocking reads let us drain both streams without the child being able
  // to fill one pipe while we wait on the other.
  setNonBlocking(stdoutPipe[0]);
  setNonBlocking(stderrPipe[0]);

  bool stdoutOpen = true;
  bool stderrOpen = true;
  while (stdoutOpen || stderrOpen) {
    struct pollfd fds[2];
    int count = 0;
    if (stdoutOpen) {
      fds[count++] = pollfd{stdoutPipe[0], POLLIN, 0};
    }
    if (stderrOpen) {
      fds[count++] = pollfd{stderrPipe[0], POLLIN, 0};
    }

    const int ready = ::poll(fds, static_cast<nfds_t>(count), -1);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    for (int i = 0; i < count; ++i) {
      if ((fds[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
        continue;
      }
      const int fd = fds[i].fd;
      const bool isStdout = fd == stdoutPipe[0];
      const bool stillOpen = readAvailable(fd, isStdout ? result.standardOutput
                                                        : result.standardError);
      if (!stillOpen) {
        if (isStdout) {
          stdoutOpen = false;
        } else {
          stderrOpen = false;
        }
        ::close(fd);
      }
    }
  }
  if (stdoutOpen) {
    ::close(stdoutPipe[0]);
  }
  if (stderrOpen) {
    ::close(stderrPipe[0]);
  }

  int status = 0;
  while (::waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      result.error = std::string("waitpid failed: ") + std::strerror(errno);
      return result;
    }
  }

  if (WIFEXITED(status)) {
    result.exitedNormally = true;
    result.exitCode = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.signaled = true;
    result.signalNumber = WTERMSIG(status);
    result.exitCode = 128 + WTERMSIG(status);
  }
  return result;
}

int runProcessInheriting(const std::vector<std::string>& argv, std::string& error) {
  if (argv.empty()) {
    error = "empty command";
    return -1;
  }
  std::vector<char*> raw = buildArgv(argv);
  const pid_t pid = ::fork();
  if (pid < 0) {
    error = std::string("fork failed: ") + std::strerror(errno);
    return -1;
  }
  if (pid == 0) {
    ::execvp(raw[0], raw.data());
    reportExecFailure(argv, errno);
    _exit(127);
  }

  int status = 0;
  while (::waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      error = std::string("waitpid failed: ") + std::strerror(errno);
      return -1;
    }
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    error = "program terminated by signal " + std::to_string(WTERMSIG(status));
    return 128 + WTERMSIG(status);
  }
  error = "program did not exit normally";
  return -1;
}

}  // namespace gable

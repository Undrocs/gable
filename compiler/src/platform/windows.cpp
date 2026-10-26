// Gable compiler -- Windows implementation of the platform abstraction.
//
// All Win32 calls use the wide-character API and convert to and from UTF-8 at
// this boundary, which is the only place in the compiler that knows about
// encodings other than UTF-8.
#if !defined(_WIN32)
#error "platform/windows.cpp must only be compiled on Windows"
#endif

#include "gable/platform.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace gable::platform {
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

std::string narrow(const std::wstring& wide) {
  if (wide.empty()) {
    return {};
  }
  const int length = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
  if (length <= 0) {
    return {};
  }
  std::string utf8(static_cast<std::size_t>(length), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(),
                        length, nullptr, nullptr);
  return utf8;
}

std::wstring temporaryDirectoryWide() {
  DWORD size = ::GetTempPathW(0, nullptr);
  if (size == 0) {
    return L".";
  }
  std::wstring buffer(static_cast<std::size_t>(size) + 1, L'\0');
  size = ::GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
  if (size == 0) {
    return L".";
  }
  buffer.resize(size);
  while (!buffer.empty() && (buffer.back() == L'\\' || buffer.back() == L'/')) {
    buffer.pop_back();
  }
  return buffer.empty() ? std::wstring(L".") : buffer;
}

}  // namespace

std::string executablePath() {
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    const DWORD length =
        ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      return {};
    }
    if (length < buffer.size()) {
      buffer.resize(length);
      return narrow(buffer);
    }
    // The buffer was too small: the documented behaviour is to grow it.
    buffer.resize(buffer.size() * 2);
  }
}

std::uint64_t processId() { return static_cast<std::uint64_t>(::GetCurrentProcessId()); }

std::string temporaryDirectoryRoot() { return narrow(temporaryDirectoryWide()); }

std::string createTemporaryDirectory(std::string& error) {
  static std::atomic<unsigned> counter{0};
  const std::wstring root = temporaryDirectoryWide();

  for (unsigned attempt = 0; attempt < 100; ++attempt) {
    const std::wstring candidate =
        root + L"\\gable-" + std::to_wstring(::GetCurrentProcessId()) + L"-" +
        std::to_wstring(counter.fetch_add(1) + attempt);
    if (::CreateDirectoryW(candidate.c_str(), nullptr) != 0) {
      return narrow(candidate);
    }
    const DWORD code = ::GetLastError();
    if (code != ERROR_ALREADY_EXISTS) {
      error = "cannot create a temporary directory: " + lastErrorMessage(code);
      return {};
    }
  }
  error = "cannot create a temporary directory: too many name collisions";
  return {};
}

std::string defaultCCompiler() { return "cl"; }

std::string executableSuffix() { return ".exe"; }

bool replaceFile(const std::string& source, const std::string& destination, std::string& error) {
  const std::wstring wideSource = widen(source);
  const std::wstring wideDestination = widen(destination);
  if (wideSource.empty() || wideDestination.empty()) {
    error = "cannot convert the file name to UTF-16";
    return false;
  }
  // MOVEFILE_REPLACE_EXISTING makes the replacement atomic from the point of
  // view of other processes reading the destination.
  if (::MoveFileExW(wideSource.c_str(), wideDestination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
    return true;
  }
  error = lastErrorMessage(::GetLastError());
  return false;
}

}  // namespace gable::platform

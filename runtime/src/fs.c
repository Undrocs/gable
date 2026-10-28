/* Gable runtime -- whole-file text input and output, and file locking.
 *
 * Failures are reported through the returned struct, never by trapping, so a
 * Gable program decides what to do about a missing file. Each result carries a
 * stable `code` in addition to a human-readable message: messages come from the
 * C library and are localised, so programs must branch on the code, not on the
 * text.
 *
 * A path may be a counted string of any bytes, but not every byte sequence is a
 * file name. Paths are validated as a whole before anything is opened (see
 * check_path), and converted to the operating system's representation here: bytes
 * on POSIX, UTF-16 on Windows.
 */
/* open() with O_CLOEXEC and nanosleep() are POSIX, and a strict C17 build hides
 * them unless the feature-test macro asks for them. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "gable/rt.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "rt_internal.h"

#define GABLE_RT_MAX_FILE_BYTES ((size_t)64 * 1024 * 1024)
/* Longest path the runtime accepts, in bytes. Longer paths are rejected with a
 * diagnostic rather than silently truncated. */
#define GABLE_RT_MAX_PATH_BYTES ((int64_t)4096)
/* How long a lock file may exist before it is treated as abandoned by a program
 * that died, how often to retry a busy lock, and how long to wait between
 * attempts. */
#define GABLE_RT_LOCK_STALE_SECONDS 10
#define GABLE_RT_LOCK_ATTEMPTS 100
#define GABLE_RT_LOCK_SLEEP_MILLISECONDS 20
/* How many names to try for the temporary file of an atomic write. */
#define GABLE_RT_TEMPORARY_ATTEMPTS 64
/* Windows refuses to replace or delete a file that another process has open, and
 * reports it as an access denial. The holders are usually a reader or a scanner
 * holding the file for an instant, so the operation is retried briefly before it
 * is reported. */
#define GABLE_RT_REPLACE_ATTEMPTS 50
#define GABLE_RT_REPLACE_SLEEP_MILLISECONDS 10

#if defined(_WIN32)
typedef wchar_t GableOsChar;
#else
typedef char GableOsChar;
#endif

/* A path in the representation the operating system expects. */
typedef struct {
  GableOsChar chars[GABLE_RT_MAX_PATH_BYTES + 1];
} GableOsPath;

/* Why a path was rejected before anything was opened. */
typedef enum {
  PATH_ACCEPTED = 0,
  PATH_IS_EMPTY,
  PATH_CONTAINS_NUL,
  PATH_IS_TOO_LONG
} PathStatus;

static gable_str literal(const char *text) {
  return gable_rt_str_copy(text, (int64_t)strlen(text));
}

static unsigned long process_id_value(void) {
#if defined(_WIN32)
  return (unsigned long)GetCurrentProcessId();
#else
  return (unsigned long)getpid();
#endif
}

static gable_str empty_string(void) {
  gable_str value;
  value.data = "";
  value.len = 0;
  return value;
}

/* A counted string may contain any byte, including NUL, but a NUL-terminated
 * system call cannot represent it: `victim\0suffix` would name `victim`. A path
 * with an embedded NUL is therefore rejected as a whole instead of being
 * truncated at the NUL. */
static PathStatus check_path(gable_str path) {
  if (path.data == NULL || path.len <= 0) {
    return PATH_IS_EMPTY;
  }
  if (path.len > GABLE_RT_MAX_PATH_BYTES) {
    return PATH_IS_TOO_LONG;
  }
  for (int64_t index = 0; index < path.len; ++index) {
    if (path.data[index] == '\0') {
      return PATH_CONTAINS_NUL;
    }
  }
  return PATH_ACCEPTED;
}

static bool reject_path(gable_str path, int32_t *code, gable_str *message) {
  switch (check_path(path)) {
    case PATH_IS_EMPTY:
      *code = GABLE_RT_FILE_INVALID_PATH;
      *message = literal("file path is empty");
      return true;
    case PATH_CONTAINS_NUL:
      *code = GABLE_RT_FILE_INVALID_PATH;
      *message = literal("file path contains a NUL byte");
      return true;
    case PATH_IS_TOO_LONG:
      *code = GABLE_RT_FILE_INVALID_PATH;
      *message = literal("file path is too long");
      return true;
    case PATH_ACCEPTED:
      return false;
  }
  return false;
}

/* Converts a Gable string into the operating system's path representation. */
static bool path_to_os(gable_str path, GableOsPath *out, int32_t *code, gable_str *message) {
  if (reject_path(path, code, message)) {
    return false;
  }
#if defined(_WIN32)
  const int wide_length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data,
                                              (int)path.len, out->chars,
                                              (int)GABLE_RT_MAX_PATH_BYTES);
  if (wide_length <= 0) {
    *code = GABLE_RT_FILE_INVALID_PATH;
    *message = literal("file path is not valid UTF-8");
    return false;
  }
  out->chars[wide_length] = L'\0';
#else
  memcpy(out->chars, path.data, (size_t)path.len);
  out->chars[path.len] = '\0';
#endif
  return true;
}

/* Maps the reason an open failed to a stable code. */
static int32_t failure_code(int error_number) {
  switch (error_number) {
    case ENOENT: return GABLE_RT_FILE_NOT_FOUND;
    case EACCES: return GABLE_RT_FILE_PERMISSION;
#ifdef ENOTDIR
    /* A path component exists but is not a directory: the path cannot name a
     * file, which is a different problem from a missing file. */
    case ENOTDIR: return GABLE_RT_FILE_INVALID_PATH;
#endif
#if defined(EPERM) && (!defined(EACCES) || EPERM != EACCES)
    case EPERM: return GABLE_RT_FILE_PERMISSION;
#endif
    default: return GABLE_RT_FILE_IO;
  }
}

/* Opens `path` for reading or writing. On failure returns NULL and fills in
 * `code` and `message`. */
static FILE *open_file(gable_str path, const char *mode, int32_t *code, gable_str *message) {
  GableOsPath os;
  if (!path_to_os(path, &os, code, message)) {
    return NULL;
  }
#if defined(_WIN32)
  const wchar_t *wide_mode = mode[0] == 'r' ? L"rb" : L"wb";
  FILE *file = _wfopen(os.chars, wide_mode);
#else
  FILE *file = fopen(os.chars, mode);
#endif
  if (file == NULL) {
    const int error_number = errno;
    *code = failure_code(error_number);
    const char *text = strerror(error_number);
    *message = gable_rt_str_copy(text, (int64_t)strlen(text));
    return NULL;
  }
  return file;
}

/* Creates `path` exclusively and returns an open descriptor, or -1. It never
 * follows an existing file or symbolic link. */
static int create_exclusive(gable_str path, int32_t *code, gable_str *message,
                            int *error_number_out) {
  GableOsPath os;
  if (!path_to_os(path, &os, code, message)) {
    return -1;
  }
#if defined(_WIN32)
  const int descriptor =
      _wopen(os.chars, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
  const int descriptor =
      open(os.chars, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
#endif
  if (descriptor < 0) {
    const int error_number = errno;
    *error_number_out = error_number;
    *code = failure_code(error_number);
    const char *text = strerror(error_number);
    *message = gable_rt_str_copy(text, (int64_t)strlen(text));
  }
  return descriptor;
}

static bool write_all(int descriptor, const char *data, size_t length) {
  size_t written = 0;
  while (written < length) {
    const size_t remaining = length - written;
#if defined(_WIN32)
    const int count = _write(descriptor, data + written,
                             (unsigned int)(remaining > 0x7FFFFFFFu ? 0x7FFFFFFFu : remaining));
#else
    const ssize_t count = write(descriptor, data + written, remaining);
#endif
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (count == 0) {
      return false;
    }
    written += (size_t)count;
  }
  return true;
}

/* Returns 0 on success. The error of a failed close is reported by the caller
 * only when nothing else failed, because close can also fail after the data was
 * already written. */
static int close_descriptor(int descriptor) {
#if defined(_WIN32)
  return _close(descriptor);
#else
  return close(descriptor);
#endif
}

static bool remove_os_file(gable_str path) {
  GableOsPath os;
  int32_t code = GABLE_RT_FILE_OK;
  gable_str message = empty_string();
  if (!path_to_os(path, &os, &code, &message)) {
    return false;
  }
#if defined(_WIN32)
  return _wremove(os.chars) == 0;
#else
  return remove(os.chars) == 0;
#endif
}

/* Age of a file in whole seconds, or -1 when it cannot be read. */
static int64_t file_age_seconds(gable_str path) {
#if defined(_WIN32)
  GableOsPath os;
  int32_t code = GABLE_RT_FILE_OK;
  gable_str message = empty_string();
  if (!path_to_os(path, &os, &code, &message)) {
    return -1;
  }
  struct _stat64 info;
  if (_wstat64(os.chars, &info) != 0) {
    return -1;
  }
#else
  GableOsPath os;
  int32_t code = GABLE_RT_FILE_OK;
  gable_str message = empty_string();
  if (!path_to_os(path, &os, &code, &message)) {
    return -1;
  }
  struct stat info;
  if (stat(os.chars, &info) != 0) {
    return -1;
  }
#endif
  return (int64_t)time(NULL) - (int64_t)info.st_mtime;
}

/* Renames `from` onto `to`, replacing `to` in one step where the platform
 * guarantees it. */
static bool rename_os_file(gable_str from, gable_str to) {
  GableOsPath source;
  GableOsPath destination;
  int32_t code = GABLE_RT_FILE_OK;
  gable_str message = empty_string();
  if (!path_to_os(from, &source, &code, &message) ||
      !path_to_os(to, &destination, &code, &message)) {
    return false;
  }
#if defined(_WIN32)
  return MoveFileExW(source.chars, destination.chars,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  return rename(source.chars, destination.chars) == 0;
#endif
}

static void sleep_milliseconds(int milliseconds) {
#if defined(_WIN32)
  Sleep((DWORD)milliseconds);
#else
  struct timespec request;
  request.tv_sec = milliseconds / 1000;
  request.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
  nanosleep(&request, NULL);
#endif
}

/* Replaces `to` with `from`. POSIX renames in one step; on Windows a file that
 * another process has open cannot be replaced at that instant, so a failure is
 * retried. */
static bool replace_os_file(gable_str from, gable_str to) {
#if defined(_WIN32)
  for (int attempt = 0; attempt < GABLE_RT_REPLACE_ATTEMPTS; ++attempt) {
    if (rename_os_file(from, to)) {
      return true;
    }
    sleep_milliseconds(GABLE_RT_REPLACE_SLEEP_MILLISECONDS);
  }
  return false;
#else
  return rename_os_file(from, to);
#endif
}

/* Removes `path`. On Windows a file that another process is looking at cannot be
 * deleted at that instant; the waiters of a lock are doing exactly that, so the
 * deletion is retried. */
static bool remove_os_file_retrying(gable_str path) {
#if defined(_WIN32)
  for (int attempt = 0; attempt < GABLE_RT_REPLACE_ATTEMPTS; ++attempt) {
    if (remove_os_file(path)) {
      return true;
    }
    sleep_milliseconds(GABLE_RT_REPLACE_SLEEP_MILLISECONDS);
  }
  return false;
#else
  return remove_os_file(path);
#endif
}

gable_rt_FileReadResult gable_rt_fs_read_text(gable_str path) {
  gable_rt_FileReadResult result;
  result.ok = false;
  result.text = empty_string();
  result.error = empty_string();
  result.code = GABLE_RT_FILE_OK;

  int32_t code = GABLE_RT_FILE_OK;
  gable_str message = empty_string();
  FILE *file = open_file(path, "rb", &code, &message);
  if (file == NULL) {
    result.code = code;
    result.error = message;
    return result;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    result.code = GABLE_RT_FILE_IO;
    result.error = literal("cannot seek in file");
    return result;
  }
  const long size = ftell(file);
  if (size < 0 || (size_t)size > GABLE_RT_MAX_FILE_BYTES) {
    fclose(file);
    result.code = (size < 0) ? GABLE_RT_FILE_IO : GABLE_RT_FILE_TOO_LARGE;
    result.error = (size < 0) ? literal("cannot determine file size")
                              : literal("file is larger than 64 MiB");
    return result;
  }
  rewind(file);

  char *buffer = (char *)gable_rt_allocate((size_t)size + 1);
  const size_t read = fread(buffer, 1, (size_t)size, file);
  const bool failed = ferror(file) != 0;
  fclose(file);
  if (failed || read != (size_t)size) {
    free(buffer);
    result.code = GABLE_RT_FILE_IO;
    result.error = literal("cannot read file");
    return result;
  }
  buffer[size] = '\0';

  result.ok = true;
  result.code = GABLE_RT_FILE_OK;
  result.text.data = buffer;
  result.text.len = (int64_t)size;
  return result;
}

gable_rt_FileWriteResult gable_rt_fs_write_text(gable_str path, gable_str text) {
  gable_rt_FileWriteResult result;
  result.ok = false;
  result.error = empty_string();
  result.code = GABLE_RT_FILE_OK;

  int32_t code = GABLE_RT_FILE_OK;
  gable_str message = empty_string();
  FILE *file = open_file(path, "wb", &code, &message);
  if (file == NULL) {
    result.code = code;
    result.error = message;
    return result;
  }
  if (text.len > 0 && text.data != NULL) {
    if (fwrite(text.data, 1, (size_t)text.len, file) != (size_t)text.len) {
      fclose(file);
      result.code = GABLE_RT_FILE_IO;
      result.error = literal("cannot write file");
      return result;
    }
  }
  if (fclose(file) != 0) {
    result.code = GABLE_RT_FILE_IO;
    result.error = literal("cannot close file");
    return result;
  }
  result.ok = true;
  return result;
}

/* Builds `path` with `suffix` appended, into caller-owned storage, and returns it
 * as a Gable string that lives until the caller returns. */
static bool build_suffixed(gable_str path, const char *suffix, char *storage, size_t capacity,
                          gable_str *out) {
  if (path.data == NULL || path.len < 0) {
    return false;
  }
  const size_t suffix_length = strlen(suffix);
  if ((size_t)path.len + suffix_length >= capacity) {
    return false;
  }
  memcpy(storage, path.data, (size_t)path.len);
  memcpy(storage + path.len, suffix, suffix_length);
  out->data = storage;
  out->len = path.len + (int64_t)suffix_length;
  return true;
}

/* True when the existing file at `path` may be replaced. Replacing a file by
 * rename only needs write permission on the directory, so a read-only file would
 * otherwise be overwritten silently; the check keeps the atomic write as strict
 * as the in-place one. A path that does not exist yet is writable in this sense,
 * and its directory decides. */
static bool destination_writable(gable_str path, int32_t *code, gable_str *message) {
  GableOsPath os;
  if (!path_to_os(path, &os, code, message)) {
    return false;
  }
#if defined(_WIN32)
  const bool writable = _waccess(os.chars, 2) == 0;
#else
  const bool writable = access(os.chars, W_OK) == 0;
#endif
  if (writable) {
    return true;
  }
  const int error_number = errno;
  if (error_number == ENOENT) {
    return true;
  }
  *code = failure_code(error_number);
  const char *text = strerror(error_number);
  *message = gable_rt_str_copy(text, (int64_t)strlen(text));
  return false;
}

/* Writes the whole file through a temporary file in the same directory, then
 * renames it into place. A program that is interrupted, or that fails halfway
 * through, leaves the previous file exactly as it was; a reader either sees the
 * old contents or the new ones. */
gable_rt_FileWriteResult gable_rt_fs_write_text_atomic(gable_str path, gable_str text) {
  gable_rt_FileWriteResult result;
  result.ok = false;
  result.error = empty_string();
  result.code = GABLE_RT_FILE_OK;

  int32_t code = GABLE_RT_FILE_OK;
  gable_str message = empty_string();
  if (!destination_writable(path, &code, &message)) {
    result.code = code;
    result.error = message;
    return result;
  }

  /* A temporary file left behind by a killed process carries the same process id
   * and a counter that restarts at one, so the name is retried until an unused
   * one is found instead of failing on the leftover. */
  static unsigned counter = 0;
  char storage[GABLE_RT_MAX_PATH_BYTES + 1];
  gable_str temporary = empty_string();
  int descriptor = -1;
  for (int attempt = 0; attempt < GABLE_RT_TEMPORARY_ATTEMPTS && descriptor < 0; ++attempt) {
    ++counter;
    char suffix[64];
    const int suffix_length = snprintf(suffix, sizeof(suffix), ".tmp-%lu-%u",
                                      process_id_value(), counter);
    if (suffix_length <= 0 || (size_t)suffix_length >= sizeof(suffix)) {
      result.code = GABLE_RT_FILE_IO;
      result.error = literal("cannot build a temporary file name");
      return result;
    }
    if (!build_suffixed(path, suffix, storage, sizeof(storage), &temporary)) {
      result.code = GABLE_RT_FILE_INVALID_PATH;
      result.error = literal("file path is too long");
      return result;
    }
    int error_number = 0;
    descriptor = create_exclusive(temporary, &code, &message, &error_number);
    /* A name is taken if the file system says the path exists, or if the
     * creation failed because it does: the two together also cover the case
     * where the file disappeared between the attempt and the check. Windows
     * reports a name that another process has open as an access denial rather
     * than a collision, which is why the question is asked of the file system as
     * well as of the error number. */
    if (descriptor < 0 && error_number != EEXIST && file_age_seconds(temporary) < 0) {
      result.code = code;
      result.error = message;
      return result;
    }
  }
  if (descriptor < 0) {
    result.code = GABLE_RT_FILE_IO;
    result.error = literal("cannot create a temporary file");
    return result;
  }

  bool wrote = text.len <= 0 || text.data == NULL ||
               write_all(descriptor, text.data, (size_t)text.len);
  if (wrote && close_descriptor(descriptor) != 0) {
    wrote = false;
  }
  if (!wrote) {
    remove_os_file(temporary);
    result.code = GABLE_RT_FILE_IO;
    result.error = literal("cannot write file");
    return result;
  }
  if (!replace_os_file(temporary, path)) {
    /* errno is not set by MoveFileExW, so on Windows the failure is reported
     * without a code the C library owns. Guessing from a stale errno would be
     * worse than saying less. */
    remove_os_file(temporary);
#if defined(_WIN32)
    result.code = GABLE_RT_FILE_IO;
    result.error = literal("cannot replace the file");
#else
    const int rename_error = errno;
    result.code = failure_code(rename_error);
    const char *text_message = strerror(rename_error);
    result.error = gable_rt_str_copy(text_message, (int64_t)strlen(text_message));
#endif
    return result;
  }

  result.ok = true;
  return result;
}

/* Creates `<path>.lock` exclusively. A lock older than GABLE_RT_LOCK_STALE_SECONDS
 * is treated as abandoned and removed, so a program that dies without unlocking
 * does not block the file forever. */
gable_rt_FileWriteResult gable_rt_fs_lock(gable_str path) {
  gable_rt_FileWriteResult result;
  result.ok = false;
  result.error = empty_string();
  result.code = GABLE_RT_FILE_OK;

  int32_t code = GABLE_RT_FILE_OK;
  gable_str message = empty_string();
  if (reject_path(path, &code, &message)) {
    result.code = code;
    result.error = message;
    return result;
  }

  char storage[GABLE_RT_MAX_PATH_BYTES + 1];
  gable_str lockPath = empty_string();
  if (!build_suffixed(path, ".lock", storage, sizeof(storage), &lockPath)) {
    result.code = GABLE_RT_FILE_INVALID_PATH;
    result.error = literal("file path is too long");
    return result;
  }

  for (int attempt = 0; attempt < GABLE_RT_LOCK_ATTEMPTS; ++attempt) {
    int error_number = 0;
    const int descriptor = create_exclusive(lockPath, &code, &message, &error_number);
    if (descriptor >= 0) {
      char owner[32];
      const int length = snprintf(owner, sizeof(owner), "%lu\n", process_id_value());
      if (length > 0) {
        (void)write_all(descriptor, owner, (size_t)length);
      }
      close_descriptor(descriptor);
      result.ok = true;
      return result;
    }
    /* Contention means the lock file exists, or that the creation failed with
     * "already exists" and the holder released it again before this check. On
     * Windows a lock file another process has open cannot be created
     * exclusively either, and the attempt fails with an access denial rather
     * than a collision, which is why the file system is asked as well. Anything
     * else - a directory that cannot be written, an invalid path - would fail
     * the same way on every attempt, so it is reported at once. */
    const int64_t age = file_age_seconds(lockPath);
    const bool contention = age >= 0 || error_number == EEXIST;
    if (!contention) {
      result.code = code;
      result.error = message;
      return result;
    }
    if (age < 0) {
      continue;  /* the holder released it while this attempt was failing */
    }
    if (age >= GABLE_RT_LOCK_STALE_SECONDS) {
      if (!remove_os_file_retrying(lockPath)) {
        /* The lock is abandoned by a program that is gone, but this process may
         * not delete it: report why creation failed instead of waiting for a
         * lock that will never be released. */
        result.code = code;
        result.error = message;
        return result;
      }
      continue;
    }
    sleep_milliseconds(GABLE_RT_LOCK_SLEEP_MILLISECONDS);
  }

  result.code = GABLE_RT_FILE_BUSY;
  result.error = literal("the file is locked by another program");
  return result;
}

gable_rt_FileWriteResult gable_rt_fs_unlock(gable_str path) {
  gable_rt_FileWriteResult result;
  result.ok = false;
  result.error = empty_string();
  result.code = GABLE_RT_FILE_OK;

  int32_t code = GABLE_RT_FILE_OK;
  gable_str message = empty_string();
  if (reject_path(path, &code, &message)) {
    result.code = code;
    result.error = message;
    return result;
  }

  char storage[GABLE_RT_MAX_PATH_BYTES + 1];
  gable_str lockPath = empty_string();
  if (!build_suffixed(path, ".lock", storage, sizeof(storage), &lockPath)) {
    result.code = GABLE_RT_FILE_INVALID_PATH;
    result.error = literal("file path is too long");
    return result;
  }

  if (remove_os_file_retrying(lockPath)) {
    result.ok = true;
    return result;
  }
  /* The error of the failed deletion is read before anything else runs, so that
   * it describes the deletion and not a later call. */
  const int error_number = errno;
  if (file_age_seconds(lockPath) < 0) {
    /* Another process released it while this one was retrying. */
    result.ok = true;
    return result;
  }
  result.code = failure_code(error_number);
  const char *text = strerror(error_number);
  result.error = gable_rt_str_copy(text, (int64_t)strlen(text));
  return result;
}

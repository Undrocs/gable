/* Tests for the file builtins of the Gable runtime.
 *
 * These call the runtime directly, which is the only way to reach some of its
 * contract: a Gable program cannot build a path that contains a NUL byte, and it
 * cannot create a 64 MiB file without help. Everything here works on disposable
 * files in a temporary directory.
 *
 * The tests are POSIX-only for now. They need fork(), chmod() and ftruncate(),
 * and none of those has been exercised on Windows, so the Windows build does not
 * register this test at all rather than running a stub that silently passes.
 *
 * Usage: fs_tests
 */
/* mkdtemp, ftruncate, utimensat and AT_FDCWD are POSIX interfaces that a strict
 * C17 build hides unless the feature-test macro asks for them. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "gable/rt.h"

static int failures = 0;
static int checks = 0;

static void fail(const char* name, const char* message) {
  ++failures;
  fprintf(stderr, "FAIL %s\n    %s\n", name, message);
}

static void check(int condition, const char* name, const char* message) {
  ++checks;
  if (!condition) {
    fail(name, message);
  }
}

static void checkCode(int32_t actual, int32_t expected, const char* name) {
  ++checks;
  if (actual != expected) {
    char message[128];
    snprintf(message, sizeof(message), "expected code %d, got %d", (int)expected, (int)actual);
    fail(name, message);
  }
}

static gable_str str_of(const char* text) {
  gable_str value;
  value.data = text;
  value.len = (int64_t)strlen(text);
  return value;
}

static gable_str bytes_of(const char* text, int64_t length) {
  gable_str value;
  value.data = text;
  value.len = length;
  return value;
}

static int running_as_root(void) { return geteuid() == 0; }

/* Builds "<scratch>/<name>" into `out`. */
static gable_str path_in(const char* scratch, const char* name, char* out, size_t capacity) {
  const int length = snprintf(out, capacity, "%s/%s", scratch, name);
  if (length <= 0 || (size_t)length >= capacity) {
    fprintf(stderr, "scratch path is too long\n");
    exit(2);
  }
  return bytes_of(out, length);
}

/* Writes "<base><suffix>" into `out`, which the caller owns. */
static char* with_suffix(const char* base, const char* suffix, char* out, size_t capacity) {
  const size_t base_length = strlen(base);
  const size_t suffix_length = strlen(suffix);
  if (base_length + suffix_length + 1 > capacity) {
    fprintf(stderr, "test path is too long\n");
    exit(2);
  }
  memcpy(out, base, base_length);
  memcpy(out + base_length, suffix, suffix_length + 1);
  return out;
}

static char* read_whole_file(const char* path, size_t* length_out) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    return NULL;
  }
  fseek(file, 0, SEEK_END);
  const long size = ftell(file);
  rewind(file);
  char* buffer = (char*)malloc((size_t)size + 1);
  const size_t read = fread(buffer, 1, (size_t)size, file);
  fclose(file);
  buffer[read] = '\0';
  if (length_out != NULL) {
    *length_out = read;
  }
  return buffer;
}

static void write_text(const char* path, const char* text) {
  FILE* file = fopen(path, "wb");
  if (file == NULL) {
    fprintf(stderr, "cannot create %s\n", path);
    exit(2);
  }
  fputs(text, file);
  fclose(file);
}

/* True when a temporary file of an atomic write is left in `scratch`. */
static int has_temporary_file(const char* scratch) {
  DIR* directory = opendir(scratch);
  if (directory == NULL) {
    return 0;
  }
  int found = 0;
  struct dirent* entry = NULL;
  while ((entry = readdir(directory)) != NULL) {
    if (strstr(entry->d_name, ".tmp-") != NULL) {
      found = 1;
      break;
    }
  }
  closedir(directory);
  return found;
}

static int file_exists(const char* path) {
  struct stat info;
  return stat(path, &info) == 0;
}

static void test_rejected_paths(const char* scratch) {
  const gable_str empty = str_of("");
  gable_str absent;
  absent.data = NULL;
  absent.len = 0;

  const gable_str emptyPaths[2] = {empty, absent};
  for (int index = 0; index < 2; ++index) {
    const gable_str path = emptyPaths[index];
    checkCode(gable_rt_fs_read_text(path).code, GABLE_RT_FILE_INVALID_PATH, "empty_path_read");
    checkCode(gable_rt_fs_write_text(path, str_of("x")).code, GABLE_RT_FILE_INVALID_PATH,
              "empty_path_write");
    checkCode(gable_rt_fs_write_text_atomic(path, str_of("x")).code, GABLE_RT_FILE_INVALID_PATH,
              "empty_path_atomic");
    checkCode(gable_rt_fs_lock(path).code, GABLE_RT_FILE_INVALID_PATH, "empty_path_lock");
    checkCode(gable_rt_fs_unlock(path).code, GABLE_RT_FILE_INVALID_PATH, "empty_path_unlock");
  }

  /* A NUL anywhere in a path names a different file to the C library, so a path
   * containing one is rejected as a whole instead of being cut short. */
  for (int position = 0; position <= 3; ++position) {
    char name[4] = {'a', 'b', 'c', '\0'};
    name[position] = '\0';
    /* The length is always four, so a NUL at the last position is an embedded
     * byte rather than the terminator the runtime is allowed to trust. */
    const gable_str path = bytes_of(name, 4);
    checkCode(gable_rt_fs_read_text(path).code, GABLE_RT_FILE_INVALID_PATH, "nul_in_path");
    checkCode(gable_rt_fs_write_text(path, str_of("x")).code, GABLE_RT_FILE_INVALID_PATH,
              "nul_in_path");
    checkCode(gable_rt_fs_write_text_atomic(path, str_of("x")).code, GABLE_RT_FILE_INVALID_PATH,
              "nul_in_path");
    checkCode(gable_rt_fs_lock(path).code, GABLE_RT_FILE_INVALID_PATH, "nul_in_path");
    checkCode(gable_rt_fs_unlock(path).code, GABLE_RT_FILE_INVALID_PATH, "nul_in_path");
  }
  (void)scratch;

  static char too_long[4100];
  memset(too_long, 'a', sizeof(too_long) - 1);
  too_long[sizeof(too_long) - 1] = '\0';
  checkCode(gable_rt_fs_read_text(bytes_of(too_long, (int64_t)sizeof(too_long) - 1)).code,
            GABLE_RT_FILE_INVALID_PATH, "too_long_path");
}

static void test_missing_and_permission(const char* scratch) {
  char path[4096];
  const gable_str missing = path_in(scratch, "missing.txt", path, sizeof(path));
  const gable_rt_FileReadResult read = gable_rt_fs_read_text(missing);
  check(!read.ok, "missing_read", "reading a missing file must fail");
  checkCode(read.code, GABLE_RT_FILE_NOT_FOUND, "missing_read");
  check(read.error.len > 0, "missing_read", "a failure must explain itself");
  check(read.text.len == 0 && read.text.data != NULL, "missing_read",
        "a failed read must return an empty string, not a null one");

  const gable_str nowhere = path_in(scratch, "no-such-directory/file.txt", path, sizeof(path));
  checkCode(gable_rt_fs_write_text(nowhere, str_of("x")).code, GABLE_RT_FILE_NOT_FOUND,
            "missing_directory_write");
  checkCode(gable_rt_fs_write_text_atomic(nowhere, str_of("x")).code, GABLE_RT_FILE_NOT_FOUND,
            "missing_directory_atomic");
  checkCode(gable_rt_fs_lock(nowhere).code, GABLE_RT_FILE_NOT_FOUND, "missing_directory_lock");

  /* Reading a directory fails; the code depends on the platform, so only the
   * failure itself is asserted. */
  const gable_str directory = bytes_of(scratch, (int64_t)strlen(scratch));
  check(!gable_rt_fs_read_text(directory).ok, "directory_read",
        "reading a directory must fail");

  if (running_as_root()) {
    fprintf(stderr, "SKIP permission_denied: tests run as root\n");
    return;
  }

  const gable_str locked_file = path_in(scratch, "read-only.txt", path, sizeof(path));
  write_text(path, "protected");
  chmod(path, S_IRUSR);
  checkCode(gable_rt_fs_read_text(locked_file).code, GABLE_RT_FILE_OK, "read_only_file_read");
  checkCode(gable_rt_fs_write_text(locked_file, str_of("x")).code, GABLE_RT_FILE_PERMISSION,
            "read_only_file_write");
  checkCode(gable_rt_fs_write_text_atomic(locked_file, str_of("x")).code,
            GABLE_RT_FILE_PERMISSION, "read_only_file_atomic");
  size_t length = 0;
  char* contents = read_whole_file(path, &length);
  check(contents != NULL && strcmp(contents, "protected") == 0, "read_only_file_atomic",
        "a rejected atomic write must leave the file untouched");
  free(contents);
  chmod(path, S_IRUSR | S_IWUSR);

  /* A file that cannot be replaced because its directory is not writable. */
  char directory_path[4096];
  const gable_str protected_directory =
      path_in(scratch, "sealed", directory_path, sizeof(directory_path));
  mkdir(directory_path, S_IRWXU);
  char inner_path[4096];
  const gable_str inner = path_in(directory_path, "data.txt", inner_path, sizeof(inner_path));
  checkCode(gable_rt_fs_write_text(inner, str_of("kept")).code, GABLE_RT_FILE_OK,
            "sealed_write_setup");
  chmod(directory_path, S_IRUSR | S_IXUSR);
  checkCode(gable_rt_fs_write_text_atomic(inner, str_of("replacement")).code,
            GABLE_RT_FILE_PERMISSION, "sealed_directory_atomic");
  chmod(directory_path, S_IRWXU);
  contents = read_whole_file(inner_path, &length);
  check(contents != NULL && strcmp(contents, "kept") == 0, "sealed_directory_atomic",
        "a failed atomic write must leave the previous contents");
  free(contents);
  (void)protected_directory;
}

static void test_atomic_write(const char* scratch) {
  char path[4096];
  const gable_str file = path_in(scratch, "atomic.txt", path, sizeof(path));

  checkCode(gable_rt_fs_write_text_atomic(file, str_of("first")).code, GABLE_RT_FILE_OK,
            "atomic_create");
  size_t length = 0;
  char* contents = read_whole_file(path, &length);
  check(contents != NULL && strcmp(contents, "first") == 0, "atomic_create",
        "the file does not contain what was written");
  free(contents);

  checkCode(gable_rt_fs_write_text_atomic(file, str_of("")).code, GABLE_RT_FILE_OK,
            "atomic_empty");
  contents = read_whole_file(path, &length);
  check(contents != NULL && length == 0, "atomic_empty",
        "an empty write must truncate the file");
  free(contents);

  checkCode(gable_rt_fs_write_text_atomic(file, str_of("second")).code, GABLE_RT_FILE_OK,
            "atomic_replace");
  contents = read_whole_file(path, &length);
  check(contents != NULL && strcmp(contents, "second") == 0, "atomic_replace",
        "the replacement contents are wrong");
  free(contents);
  check(!has_temporary_file(scratch), "atomic_replace", "a temporary file was left behind");

  /* Temporary files left behind by killed processes carry this process's id and
   * counter values that a restarted process reuses, so the next write has to step
   * past them instead of failing. */
  char leftovers[8][4096];
  for (int index = 0; index < 8; ++index) {
    char suffix[32];
    snprintf(suffix, sizeof(suffix), ".tmp-%lu-%d", (unsigned long)getpid(), index + 1);
    with_suffix(path, suffix, leftovers[index], sizeof(leftovers[index]));
    write_text(leftovers[index], "half-written");
  }
  checkCode(gable_rt_fs_write_text_atomic(file, str_of("third")).code, GABLE_RT_FILE_OK,
            "atomic_leftover_temporary");
  contents = read_whole_file(path, &length);
  check(contents != NULL && strcmp(contents, "third") == 0, "atomic_leftover_temporary",
        "the file was not written");
  free(contents);
  for (int index = 0; index < 8; ++index) {
    contents = read_whole_file(leftovers[index], &length);
    check(contents != NULL && strcmp(contents, "half-written") == 0,
          "atomic_leftover_temporary", "a foreign temporary file was modified");
    free(contents);
    remove(leftovers[index]);
  }
}

static void test_locking(const char* scratch) {
  char path[4096];
  const gable_str file = path_in(scratch, "locked.txt", path, sizeof(path));
  char lock_path[4096];
  with_suffix(path, ".lock", lock_path, sizeof(lock_path));

  checkCode(gable_rt_fs_unlock(file).code, GABLE_RT_FILE_OK, "unlock_without_lock");
  checkCode(gable_rt_fs_lock(file).code, GABLE_RT_FILE_OK, "lock");
  check(file_exists(lock_path), "lock", "the lock file was not created");
  checkCode(gable_rt_fs_lock(file).code, GABLE_RT_FILE_BUSY, "lock_twice");
  checkCode(gable_rt_fs_unlock(file).code, GABLE_RT_FILE_OK, "unlock");
  check(!file_exists(lock_path), "unlock", "the lock file was not removed");
  checkCode(gable_rt_fs_lock(file).code, GABLE_RT_FILE_OK, "lock_after_unlock");
  checkCode(gable_rt_fs_unlock(file).code, GABLE_RT_FILE_OK, "unlock_again");

  /* A lock left behind by a killed process is reclaimed once it is old enough. */
  write_text(lock_path, "1\n");
  checkCode(gable_rt_fs_lock(file).code, GABLE_RT_FILE_BUSY, "fresh_foreign_lock");
  struct timespec times[2];
  times[0].tv_sec = time(NULL) - 30;
  times[0].tv_nsec = 0;
  times[1] = times[0];
  utimensat(AT_FDCWD, lock_path, times, 0);
  checkCode(gable_rt_fs_lock(file).code, GABLE_RT_FILE_OK, "stale_lock_reclaimed");
  checkCode(gable_rt_fs_unlock(file).code, GABLE_RT_FILE_OK, "stale_lock_unlock");

  /* A lock held by another process is reported as busy, not waited on forever. */
  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) {
    fail("cross_process_lock", "cannot create a pipe");
    return;
  }
  const pid_t child = fork();
  if (child < 0) {
    fail("cross_process_lock", "cannot fork");
    return;
  }
  if (child == 0) {
    close(pipe_fds[0]);
    const gable_rt_FileWriteResult locked = gable_rt_fs_lock(file);
    const char status = locked.ok ? 'y' : 'n';
    (void)write(pipe_fds[1], &status, 1);
    sleep(2);
    _exit(0);
  }
  close(pipe_fds[1]);
  char status = 'n';
  if (read(pipe_fds[0], &status, 1) != 1) {
    status = 'n';
  }
  close(pipe_fds[0]);
  check(status == 'y', "cross_process_lock", "the child could not take the lock");
  checkCode(gable_rt_fs_lock(file).code, GABLE_RT_FILE_BUSY, "cross_process_lock");
  int status_word = 0;
  waitpid(child, &status_word, 0);
  checkCode(gable_rt_fs_unlock(file).code, GABLE_RT_FILE_OK, "cross_process_unlock");
}

static void test_oversized_file(const char* scratch) {
  char path[4096];
  path_in(scratch, "large.txt", path, sizeof(path));
  FILE* file = fopen(path, "wb");
  if (file == NULL) {
    fail("oversized_file", "cannot create the test file");
    return;
  }
  /* Sparse: the file is only as large as the file system needs it to be. */
  if (ftruncate(fileno(file), (off_t)(64 * 1024 * 1024) + 1) != 0) {
    fclose(file);
    fail("oversized_file", "cannot grow the test file");
    return;
  }
  fclose(file);
  const gable_str large = bytes_of(path, (int64_t)strlen(path));
  const gable_rt_FileReadResult read = gable_rt_fs_read_text(large);
  check(!read.ok, "oversized_file", "a file larger than 64 MiB must be rejected");
  checkCode(read.code, GABLE_RT_FILE_TOO_LARGE, "oversized_file");
  remove(path);
}

/* Removes the files the tests created and then the directory itself. */
static void remove_scratch_directory(const char* scratch) {
  DIR* directory = opendir(scratch);
  if (directory != NULL) {
    struct dirent* entry = NULL;
    while ((entry = readdir(directory)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }
      char path[4096];
      with_suffix(scratch, "/", path, sizeof(path));
      with_suffix(path, entry->d_name, path, sizeof(path));
      if (rmdir(path) != 0) {
        remove(path);
      }
    }
    closedir(directory);
  }
  rmdir(scratch);
}

int main(void) {
  char scratch[] = "/tmp/gable-fs-tests-XXXXXX";
  if (mkdtemp(scratch) == NULL) {
    fprintf(stderr, "cannot create a temporary directory\n");
    return 2;
  }

  test_rejected_paths(scratch);
  test_missing_and_permission(scratch);
  test_atomic_write(scratch);
  test_locking(scratch);
  test_oversized_file(scratch);

  remove_scratch_directory(scratch);

  printf("%s %d/%d runtime file tests\n", failures == 0 ? "PASS" : "FAILED", checks - failures,
         checks);
  return failures == 0 ? 0 : 1;
}

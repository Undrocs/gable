/* Gable runtime -- whole-file text input and output.
 *
 * Failures are reported through the returned struct, never by trapping, so a
 * Gable program decides what to do about a missing file. */
#include "gable/rt.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt_internal.h"

#define GABLE_RT_MAX_FILE_BYTES ((size_t)64 * 1024 * 1024)

/* Paths arrive as counted strings. The runtime needs NUL-terminated names for
 * the C library, so it copies the bytes into a bounded buffer instead of
 * assuming anything about the caller's storage. */
static bool path_to_c_string(gable_str path, char *buffer, size_t size) {
  if (path.data == NULL || path.len <= 0 || (size_t)path.len >= size) {
    return false;
  }
  memcpy(buffer, path.data, (size_t)path.len);
  buffer[path.len] = '\0';
  return true;
}

static gable_str empty_string(void) {
  gable_str value;
  value.data = "";
  value.len = 0;
  return value;
}

gable_rt_FileReadResult gable_rt_fs_read_text(gable_str path) {
  gable_rt_FileReadResult result;
  result.ok = false;
  result.text = empty_string();
  result.error = empty_string();

  char name[4096];
  if (!path_to_c_string(path, name, sizeof(name))) {
    result.error = gable_rt_str_copy("invalid file path", 17);
    return result;
  }

  FILE *file = fopen(name, "rb");
  if (file == NULL) {
    const char *message = strerror(errno);
    result.error = gable_rt_str_copy(message, (int64_t)strlen(message));
    return result;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    result.error = gable_rt_str_copy("cannot seek in file", 19);
    return result;
  }
  const long size = ftell(file);
  if (size < 0 || (size_t)size > GABLE_RT_MAX_FILE_BYTES) {
    fclose(file);
    result.error = (size < 0) ? gable_rt_str_copy("cannot determine file size", 26)
                              : gable_rt_str_copy("file is larger than 64 MiB", 25);
    return result;
  }
  rewind(file);

  char *buffer = (char *)gable_rt_allocate((size_t)size + 1);
  const size_t read = fread(buffer, 1, (size_t)size, file);
  const bool failed = ferror(file) != 0;
  fclose(file);
  if (failed || read != (size_t)size) {
    free(buffer);
    result.error = gable_rt_str_copy("cannot read file", 16);
    return result;
  }
  buffer[size] = '\0';

  result.ok = true;
  result.text.data = buffer;
  result.text.len = (int64_t)size;
  return result;
}

gable_rt_FileWriteResult gable_rt_fs_write_text(gable_str path, gable_str text) {
  gable_rt_FileWriteResult result;
  result.ok = false;
  result.error = empty_string();

  char name[4096];
  if (!path_to_c_string(path, name, sizeof(name))) {
    result.error = gable_rt_str_copy("invalid file path", 17);
    return result;
  }

  FILE *file = fopen(name, "wb");
  if (file == NULL) {
    const char *message = strerror(errno);
    result.error = gable_rt_str_copy(message, (int64_t)strlen(message));
    return result;
  }
  if (text.len > 0 && text.data != NULL) {
    if (fwrite(text.data, 1, (size_t)text.len, file) != (size_t)text.len) {
      fclose(file);
      result.error = gable_rt_str_copy("cannot write file", 17);
      return result;
    }
  }
  if (fclose(file) != 0) {
    result.error = gable_rt_str_copy("cannot close file", 17);
    return result;
  }
  result.ok = true;
  return result;
}

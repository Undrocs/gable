/* Gable runtime -- string operations. */
#include "gable/rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt_internal.h"

gable_str gable_rt_str(const char *data, int64_t len) {
  gable_str value;
  value.data = data;
  value.len = len;
  return value;
}

bool gable_rt_str_eq(gable_str lhs, gable_str rhs) {
  if (lhs.len != rhs.len) {
    return false;
  }
  if (lhs.len == 0) {
    return true;
  }
  if (lhs.data == NULL || rhs.data == NULL) {
    return false;
  }
  return memcmp(lhs.data, rhs.data, (size_t)lhs.len) == 0;
}

/* Copies `len` bytes so the result does not alias its input. Allocation failure
 * terminates the program: there is no way to report it through the language. */
gable_str gable_rt_str_copy(const char *data, int64_t len) {
  char *buffer = (char *)gable_rt_allocate(len == 0 ? 1 : (size_t)len);
  if (len > 0 && data != NULL) {
    memcpy(buffer, data, (size_t)len);
  }
  gable_str value;
  value.data = buffer;
  value.len = len;
  return value;
}

gable_str gable_rt_str_concat(gable_str lhs, gable_str rhs) {
  if (lhs.len == 0) {
    return gable_rt_str_copy(rhs.data, rhs.len);
  }
  if (rhs.len == 0) {
    return gable_rt_str_copy(lhs.data, lhs.len);
  }
  const size_t total = (size_t)lhs.len + (size_t)rhs.len;
  char *buffer = (char *)gable_rt_allocate(total);
  memcpy(buffer, lhs.data, (size_t)lhs.len);
  memcpy(buffer + lhs.len, rhs.data, (size_t)rhs.len);
  gable_str value;
  value.data = buffer;
  value.len = lhs.len + rhs.len;
  return value;
}

gable_str gable_rt_str_of_i64(int64_t value) {
  char buffer[32];
  const int written = snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
  return gable_rt_str_copy(buffer, written);
}

gable_str gable_rt_str_of_bool(bool value) {
  return value ? gable_rt_str_copy("true", 4) : gable_rt_str_copy("false", 5);
}

bool gable_rt_is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

/* Parses an optionally signed decimal integer. Leading and trailing whitespace
 * is ignored; anything else, an empty string, or overflow makes the parse fail
 * rather than silently truncating. */
gable_rt_ParseResult gable_rt_parse_i64(gable_str text) {
  gable_rt_ParseResult result;
  result.ok = false;
  result.value = 0;

  int64_t begin = 0;
  int64_t end = text.len;
  while (begin < end && text.data != NULL && gable_rt_is_space(text.data[begin])) {
    ++begin;
  }
  while (end > begin && text.data != NULL && gable_rt_is_space(text.data[end - 1])) {
    --end;
  }
  if (begin >= end || text.data == NULL) {
    return result;
  }

  bool negative = false;
  if (text.data[begin] == '+' || text.data[begin] == '-') {
    negative = text.data[begin] == '-';
    ++begin;
    if (begin >= end) {
      return result;
    }
  }

  /* Accumulate as a negative number so INT64_MIN is representable. */
  int64_t magnitude = 0;
  for (int64_t i = begin; i < end; ++i) {
    const char c = text.data[i];
    if (!is_digit(c)) {
      return result;
    }
    const int digit = c - '0';
    if (magnitude < (INT64_MIN + digit) / 10) {
      return result; /* overflow */
    }
    magnitude = magnitude * 10 - digit;
  }

  if (!negative) {
    if (magnitude == INT64_MIN) {
      return result; /* overflow */
    }
    magnitude = -magnitude;
  }

  result.ok = true;
  result.value = magnitude;
  return result;
}

gable_rt_FindResult gable_rt_str_find(gable_str haystack, gable_str needle) {
  gable_rt_FindResult result;
  result.ok = false;
  result.index = 0;
  if (needle.len == 0) {
    result.ok = true;
    result.index = 0;
    return result;
  }
  if (haystack.data == NULL || needle.data == NULL || needle.len > haystack.len) {
    return result;
  }
  for (int64_t i = 0; i + needle.len <= haystack.len; ++i) {
    if (memcmp(haystack.data + i, needle.data, (size_t)needle.len) == 0) {
      result.ok = true;
      result.index = i;
      return result;
    }
  }
  return result;
}

gable_rt_SliceResult gable_rt_str_slice(gable_str text, int64_t start, int64_t length) {
  gable_rt_SliceResult result;
  result.ok = false;
  result.text = text;
  if (start < 0 || length < 0 || start > text.len || length > text.len - start) {
    return result;
  }
  result.ok = true;
  result.text = gable_rt_str_copy(text.data == NULL ? "" : text.data + start, length);
  return result;
}

gable_str gable_rt_str_trim(gable_str text) {
  if (text.data == NULL) {
    return text;
  }
  int64_t begin = 0;
  int64_t end = text.len;
  while (begin < end && gable_rt_is_space(text.data[begin])) {
    ++begin;
  }
  while (end > begin && gable_rt_is_space(text.data[end - 1])) {
    --end;
  }
  /* Trimming always copies, so the result never aliases a caller's buffer that
   * may be modified afterwards. */
  return gable_rt_str_copy(text.data + begin, end - begin);
}

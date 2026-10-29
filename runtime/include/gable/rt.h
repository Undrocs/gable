/* Gable runtime -- C ABI.
 *
 * The runtime is written in C17 and exposes only C types across its boundary.
 * It is linked into every produced executable; it is not part of the language
 * definition, but the behaviour it implements is specified in
 * docs/specification.md.
 *
 * Prelude structs declared in compiler/include/gable/prelude.h are defined here
 * with the same field order. That order is part of the ABI between generated
 * code and this library.
 */
#ifndef GABLE_RT_H
#define GABLE_RT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(_MSC_VER)
#define GABLE_NORETURN __declspec(noreturn)
#elif defined(__cplusplus)
#define GABLE_NORETURN [[noreturn]]
#else
#define GABLE_NORETURN _Noreturn
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Exit status used when a Gable program is terminated by a runtime trap.
 * 70 is EX_SOFTWARE from sysexits.h. */
#define GABLE_RT_TRAP_EXIT_CODE 70

/* Strings are immutable UTF-8 byte sequences with an explicit length, so they
 * are not NUL-terminated and may contain NUL bytes. `len` counts bytes, not
 * Unicode code points. */
typedef struct {
  const char *data;
  int64_t len;
} gable_str;

/* A growable list. A list value is a handle to shared storage: copying the value
 * copies the reference, so every copy sees the same elements, and `list_push`
 * on one is visible through all of them. The elements are stored `stride` bytes
 * apart, which is how one implementation serves every element type. */
typedef struct gable_rt_list {
  void *data;
  int64_t len;
  int64_t capacity;
  int64_t stride;
} *gable_list;

/* Prelude structs. Field order matches compiler/include/gable/prelude.h. */
typedef struct {
  bool ok;
  gable_str text;
  gable_str error;
  int64_t code;
} gable_rt_FileReadResult;

typedef struct {
  bool ok;
  gable_str error;
  int64_t code;
} gable_rt_FileWriteResult;

typedef struct {
  bool ok;
  int64_t value;
} gable_rt_ParseResult;

typedef struct {
  bool ok;
  int64_t index;
} gable_rt_FindResult;

typedef struct {
  bool ok;
  gable_str text;
} gable_rt_SliceResult;

/* Stable reason codes for the file results. Programs branch on these; the
 * `error` text comes from the C library and is localised. */
#define GABLE_RT_FILE_OK 0
#define GABLE_RT_FILE_NOT_FOUND 1
#define GABLE_RT_FILE_PERMISSION 2
#define GABLE_RT_FILE_INVALID_PATH 3
#define GABLE_RT_FILE_TOO_LARGE 4
#define GABLE_RT_FILE_IO 5
#define GABLE_RT_FILE_BUSY 6

/* Prepares the runtime before a Gable program runs; the generated entry point
 * calls this first. It records where the running thread's stack begins, so the
 * collector can find the values that are still in use, and arranges for the
 * runtime's memory to be released at exit (docs/specification.md, "Memory"). */
void gable_rt_init(void);

/* Building strings from a literal that the compiler stores in the binary. */
gable_str gable_rt_str(const char *data, int64_t len);

/* Output. One value followed by a newline; `eprint` writes to standard error. */
void gable_rt_print_str(gable_str value);
void gable_rt_print_bool(bool value);
void gable_rt_print_i64(int64_t value);
void gable_rt_print_u64(uint64_t value);
void gable_rt_eprint_str(gable_str value);
void gable_rt_eprint_bool(bool value);
void gable_rt_eprint_i64(int64_t value);
void gable_rt_eprint_u64(uint64_t value);

/* Narrowing integer conversions, used by generated code.
 *
 * Gable arithmetic is defined at the width of its operands: a result keeps the
 * low bits that its type can hold. C instead promotes narrow operands to `int`,
 * so `(uint8_t)255 + 1` is 256 and `(uint16_t)x * y` can overflow `int` (which
 * is undefined behaviour). Generated code therefore computes in `uint64_t` and
 * reduces through one of these helpers, which makes the result independent of
 * the int width of the host and of the C compiler's conversion rules.
 *
 * The signed helpers reinterpret the low bits through `memcpy` rather than a
 * cast, because converting an out-of-range unsigned value to a signed type is
 * implementation-defined. */
static inline uint8_t gable_rt_trunc_u8(uint64_t bits) {
  return (uint8_t)(bits & UINT64_C(0xFF));
}

static inline uint16_t gable_rt_trunc_u16(uint64_t bits) {
  return (uint16_t)(bits & UINT64_C(0xFFFF));
}

static inline uint32_t gable_rt_trunc_u32(uint64_t bits) {
  return (uint32_t)(bits & UINT64_C(0xFFFFFFFF));
}

static inline uint64_t gable_rt_trunc_u64(uint64_t bits) { return bits; }

static inline int8_t gable_rt_trunc_i8(uint64_t bits) {
  const uint8_t raw = (uint8_t)(bits & UINT64_C(0xFF));
  int8_t result;
  memcpy(&result, &raw, sizeof result);
  return result;
}

static inline int16_t gable_rt_trunc_i16(uint64_t bits) {
  const uint16_t raw = (uint16_t)(bits & UINT64_C(0xFFFF));
  int16_t result;
  memcpy(&result, &raw, sizeof result);
  return result;
}

static inline int32_t gable_rt_trunc_i32(uint64_t bits) {
  const uint32_t raw = (uint32_t)(bits & UINT64_C(0xFFFFFFFF));
  int32_t result;
  memcpy(&result, &raw, sizeof result);
  return result;
}

static inline int64_t gable_rt_trunc_i64(uint64_t bits) {
  int64_t result;
  memcpy(&result, &bits, sizeof result);
  return result;
}

/* Integer division and remainder.
 *
 * Both helpers terminate the program with a diagnostic on standard error and
 * exit status GABLE_RT_TRAP_EXIT_CODE when the divisor is zero. For signed
 * operands, the single overflowing case (INT64_MIN / -1) wraps to INT64_MIN,
 * matching Gable's defined wrapping arithmetic; remainder is then zero. */
int64_t gable_rt_div_i64(int64_t lhs, int64_t rhs);
int64_t gable_rt_rem_i64(int64_t lhs, int64_t rhs);
uint64_t gable_rt_div_u64(uint64_t lhs, uint64_t rhs);
uint64_t gable_rt_rem_u64(uint64_t lhs, uint64_t rhs);

/* Arrays are bounds checked by generated code before an element is read or
 * written; out-of-range access traps through this function so the message and
 * exit status are defined by the runtime. */
GABLE_NORETURN void gable_rt_index_error(int64_t index, int64_t length);

/* Creates an empty list whose elements are `element_size` bytes each. */
gable_list gable_rt_list_new(int64_t element_size);
/* Appends a copy of `*element`. */
void gable_rt_list_push(gable_list list, const void *element);
/* Address of element `index`; traps when the index is out of range. */
void *gable_rt_list_at(gable_list list, int64_t index);
int64_t gable_rt_list_len(gable_list list);

/* Splits `text` at every occurrence of `separator`, returning the pieces as a
 * list of strings. An empty separator splits between every byte. Pieces are
 * copies, so they stay valid after the input is gone. */
gable_list gable_rt_str_split(gable_str text, gable_str separator);
/* Joins `parts` with `separator` between them. The result is a new string. */
gable_str gable_rt_str_join(gable_list parts, gable_str separator);

/* Strings. Concatenation allocates on the collector-managed heap and the result
 * lives until the program can no longer reach it (docs/specification.md §12). */
bool gable_rt_str_eq(gable_str lhs, gable_str rhs);
gable_str gable_rt_str_concat(gable_str lhs, gable_str rhs);
gable_str gable_rt_str_of_i64(int64_t value);
gable_str gable_rt_str_of_bool(bool value);
gable_rt_ParseResult gable_rt_parse_i64(gable_str text);
gable_rt_FindResult gable_rt_str_find(gable_str haystack, gable_str needle);
gable_rt_SliceResult gable_rt_str_slice(gable_str text, int64_t start, int64_t length);
gable_str gable_rt_str_trim(gable_str text);

/* Process arguments, made available by the generated entry point.
 *
 * On Windows the arguments are converted from the process ANSI code page to
 * UTF-8, because Gable strings are UTF-8. POSIX hosts pass byte sequences
 * through unchanged. */
void gable_rt_set_args(int argc, char **argv);
int64_t gable_rt_arg_count(void);
gable_str gable_rt_arg(int64_t index);

/* Files. The whole file is read into memory; failures are reported through the
 * returned struct rather than by trapping. */
gable_rt_FileReadResult gable_rt_fs_read_text(gable_str path);
gable_rt_FileWriteResult gable_rt_fs_write_text(gable_str path, gable_str text);

/* Writes the whole file through a temporary file in the same directory and
 * renames it into place, so an interrupted or failing write leaves the previous
 * contents untouched. */
gable_rt_FileWriteResult gable_rt_fs_write_text_atomic(gable_str path, gable_str text);

/* Advisory lock for read-modify-write sequences: creates `<path>.lock`
 * exclusively, retrying for about two seconds, and treats a lock older than ten
 * seconds as abandoned. `fs_unlock` removes it. Neither function touches the data
 * file itself. */
gable_rt_FileWriteResult gable_rt_fs_lock(gable_str path);
gable_rt_FileWriteResult gable_rt_fs_unlock(gable_str path);

/* Terminates the program after writing a runtime diagnostic to standard
 * error. Never returns. */
GABLE_NORETURN void gable_rt_trap(const char *message);

#ifdef __cplusplus
}
#endif

#endif /* GABLE_RT_H */

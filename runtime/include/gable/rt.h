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

/* Prelude structs. Field order matches compiler/include/gable/prelude.h. */
typedef struct {
  bool ok;
  gable_str text;
  gable_str error;
} gable_rt_FileReadResult;

typedef struct {
  bool ok;
  gable_str error;
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

/* Strings. Concatenation allocates and the result lives until the process
 * exits: Gable v0.2 has no memory reclamation (docs/specification.md). */
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
int32_t gable_rt_arg_count(void);
gable_str gable_rt_arg(int32_t index);

/* Files. The whole file is read into memory; failures are reported through the
 * returned struct rather than by trapping. */
gable_rt_FileReadResult gable_rt_fs_read_text(gable_str path);
gable_rt_FileWriteResult gable_rt_fs_write_text(gable_str path, gable_str text);

/* Terminates the program after writing a runtime diagnostic to standard
 * error. Never returns. */
GABLE_NORETURN void gable_rt_trap(const char *message);

#ifdef __cplusplus
}
#endif

#endif /* GABLE_RT_H */

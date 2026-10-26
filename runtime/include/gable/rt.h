/* Gable runtime -- C ABI.
 *
 * The runtime is written in C17 and exposes only C types across its boundary.
 * It is linked into every produced executable; it is not part of the language
 * definition, but the behaviour it implements (output formatting, division
 * traps) is specified in docs/specification.md.
 */
#ifndef GABLE_RT_H
#define GABLE_RT_H

#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
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

/* Writes one value followed by a newline to standard output. */
void gable_rt_print_str(const char *value);
void gable_rt_print_bool(bool value);
void gable_rt_print_i64(int64_t value);
void gable_rt_print_u64(uint64_t value);

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

/* Terminates the program after writing a runtime diagnostic to standard
 * error. Never returns. */
GABLE_NORETURN void gable_rt_trap(const char *message);

#ifdef __cplusplus
}
#endif

#endif /* GABLE_RT_H */

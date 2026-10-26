/* Gable runtime -- runtime traps and integer division semantics. */
#include "gable/rt.h"

#include <stdio.h>
#include <stdlib.h>

void gable_rt_trap(const char *message) {
  /* Flush standard output first so that output produced before the trap is
   * not lost when the exit status is observed by a parent process. */
  fflush(stdout);
  fprintf(stderr, "gable: runtime error: %s\n", message != NULL ? message : "unspecified failure");
  fflush(stderr);
  exit(GABLE_RT_TRAP_EXIT_CODE);
}

static _Noreturn void trap_divide_by_zero(void) {
  gable_rt_trap("integer division by zero");
}

int64_t gable_rt_div_i64(int64_t lhs, int64_t rhs) {
  if (rhs == 0) {
    trap_divide_by_zero();
  }
  if (rhs == -1) {
    /* Negation in the unsigned domain: wraps for INT64_MIN instead of being
     * undefined in C. */
    return (int64_t)(0u - (uint64_t)lhs);
  }
  return lhs / rhs;
}

int64_t gable_rt_rem_i64(int64_t lhs, int64_t rhs) {
  if (rhs == 0) {
    trap_divide_by_zero();
  }
  if (rhs == -1) {
    return 0;
  }
  return lhs % rhs;
}

uint64_t gable_rt_div_u64(uint64_t lhs, uint64_t rhs) {
  if (rhs == 0) {
    trap_divide_by_zero();
  }
  return lhs / rhs;
}

uint64_t gable_rt_rem_u64(uint64_t lhs, uint64_t rhs) {
  if (rhs == 0) {
    trap_divide_by_zero();
  }
  return lhs % rhs;
}

/* Gable runtime -- standard output. */
#include "gable/rt.h"

#include <inttypes.h>
#include <stdio.h>

void gable_rt_print_str(const char *value) {
  if (value == NULL) {
    value = "(null)";
  }
  fputs(value, stdout);
  fputc('\n', stdout);
}

void gable_rt_print_bool(bool value) {
  fputs(value ? "true\n" : "false\n", stdout);
}

void gable_rt_print_i64(int64_t value) {
  printf("%" PRId64 "\n", value);
}

void gable_rt_print_u64(uint64_t value) {
  printf("%" PRIu64 "\n", value);
}

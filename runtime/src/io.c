/* Gable runtime -- standard output, standard error, and allocation. */
#include "gable/rt.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "rt_internal.h"

void *gable_rt_allocate(size_t bytes) {
  void *memory = malloc(bytes == 0 ? 1 : bytes);
  if (memory == NULL) {
    gable_rt_trap("out of memory");
  }
  return memory;
}

void gable_rt_write(gable_str value, bool to_stderr, bool newline) {
  FILE *stream = to_stderr ? stderr : stdout;
  if (value.data != NULL && value.len > 0) {
    fwrite(value.data, 1, (size_t)value.len, stream);
  }
  if (newline) {
    fputc('\n', stream);
  }
}

void gable_rt_print_str(gable_str value) { gable_rt_write(value, false, true); }

void gable_rt_print_bool(bool value) {
  fputs(value ? "true\n" : "false\n", stdout);
}

void gable_rt_print_i64(int64_t value) { printf("%" PRId64 "\n", value); }

void gable_rt_print_u64(uint64_t value) { printf("%" PRIu64 "\n", value); }

void gable_rt_eprint_str(gable_str value) { gable_rt_write(value, true, true); }

void gable_rt_eprint_bool(bool value) { fputs(value ? "true\n" : "false\n", stderr); }

void gable_rt_eprint_i64(int64_t value) { fprintf(stderr, "%" PRId64 "\n", value); }

void gable_rt_eprint_u64(uint64_t value) {
  fprintf(stderr, "%" PRIu64 "\n", value);
}

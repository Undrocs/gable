/* Gable runtime -- standard output and standard error. */
#include "gable/rt.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#include "rt_internal.h"

/* Output must be byte-identical on every host, so the standard streams are put
 * into binary mode on Windows: without it a redirected stream translates every
 * newline to CRLF, which would make program output host-dependent. */
static void prepare_standard_streams(void) {
#if defined(_WIN32)
  static bool prepared = false;
  if (!prepared) {
    (void)_setmode(_fileno(stdout), _O_BINARY);
    (void)_setmode(_fileno(stderr), _O_BINARY);
    prepared = true;
  }
#endif
}

void gable_rt_write(gable_str value, bool to_stderr, bool newline) {
  prepare_standard_streams();
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
  prepare_standard_streams();
  fputs(value ? "true\n" : "false\n", stdout);
}

void gable_rt_print_i64(int64_t value) {
  prepare_standard_streams();
  printf("%" PRId64 "\n", value);
}

void gable_rt_print_u64(uint64_t value) {
  prepare_standard_streams();
  printf("%" PRIu64 "\n", value);
}

void gable_rt_eprint_str(gable_str value) { gable_rt_write(value, true, true); }

void gable_rt_eprint_bool(bool value) {
  prepare_standard_streams();
  fputs(value ? "true\n" : "false\n", stderr);
}

void gable_rt_eprint_i64(int64_t value) { fprintf(stderr, "%" PRId64 "\n", value); }

void gable_rt_eprint_u64(uint64_t value) {
  fprintf(stderr, "%" PRIu64 "\n", value);
}

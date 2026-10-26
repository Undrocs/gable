/* Gable runtime -- process arguments.
 *
 * The generated entry point hands the process arguments to the runtime before
 * calling the Gable `main`, so `arg_count` and `arg` need no language-level
 * array of strings. */
#include "gable/rt.h"

#include <stddef.h>
#include <string.h>

#include "rt_internal.h"

static int g_argc = 0;
static char **g_argv = NULL;

void gable_rt_set_args(int argc, char **argv) {
  g_argc = argc;
  g_argv = argv;
}

int32_t gable_rt_arg_count(void) { return (int32_t)g_argc; }

gable_str gable_rt_arg(int32_t index) {
  gable_str value;
  value.data = "";
  value.len = 0;
  if (g_argv == NULL || index < 0 || index >= g_argc) {
    return value;
  }
  const char *argument = g_argv[index];
  if (argument == NULL) {
    return value;
  }
  value.data = argument;
  value.len = (int64_t)strlen(argument);
  return value;
}

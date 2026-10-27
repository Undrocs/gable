/* Gable runtime -- process arguments.
 *
 * The generated entry point hands the process arguments to the runtime before
 * calling the Gable `main`, so `arg_count` and `arg` need no language-level
 * array of strings.
 *
 * On Windows the C runtime supplies arguments in the process ANSI code page
 * while Gable strings are UTF-8, so they are converted once here. The converted
 * strings live for the lifetime of the process.
 */
#include "gable/rt.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "rt_internal.h"

#if defined(_WIN32)
#include <windows.h>
#endif

static int g_argc = 0;
static char **g_argv = NULL;
#if defined(_WIN32)
static char **g_converted_argv = NULL;
#endif

#if defined(_WIN32)
static void convert_to_utf8(int argc, char **argv) {
  if (argc <= 0 || argv == NULL) {
    return;
  }
  char **converted = (char **)malloc(sizeof(char *) * (size_t)argc);
  if (converted == NULL) {
    return;
  }
  for (int i = 0; i < argc; ++i) {
    converted[i] = argv[i];
    if (argv[i] == NULL) {
      continue;
    }
    const int wideLength = MultiByteToWideChar(CP_ACP, 0, argv[i], -1, NULL, 0);
    if (wideLength <= 0) {
      continue;
    }
    wchar_t *wide = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)wideLength);
    if (wide == NULL) {
      continue;
    }
    if (MultiByteToWideChar(CP_ACP, 0, argv[i], -1, wide, wideLength) > 0) {
      const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
      if (utf8Length > 0) {
        char *utf8 = (char *)malloc((size_t)utf8Length);
        if (utf8 != NULL &&
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, utf8Length, NULL, NULL) > 0) {
          converted[i] = utf8;
        } else {
          free(utf8);
        }
      }
    }
    free(wide);
  }
  g_converted_argv = converted;
}
#endif

void gable_rt_set_args(int argc, char **argv) {
  g_argc = argc;
  g_argv = argv;
#if defined(_WIN32)
  convert_to_utf8(argc, argv);
  if (g_converted_argv != NULL) {
    g_argv = g_converted_argv;
  }
#endif
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

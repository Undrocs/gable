/* Gable runtime -- internal helpers shared by the runtime translation units.
 * Not part of the public ABI; generated code never refers to these. */
#ifndef GABLE_RT_INTERNAL_H
#define GABLE_RT_INTERNAL_H

#include <stdint.h>

#include "gable/rt.h"

/* Allocates `bytes` (at least 1) or terminates the program. */
void *gable_rt_allocate(size_t bytes);

/* Copies `len` bytes. */
gable_str gable_rt_str_copy(const char *data, int64_t len);

bool gable_rt_is_space(char c);

/* Writes `len` bytes to the given stream, followed by a newline. */
void gable_rt_write(gable_str value, bool to_stderr, bool newline);

#endif /* GABLE_RT_INTERNAL_H */

/* Gable runtime -- internal helpers shared by the runtime translation units.
 * Not part of the public ABI; generated code never refers to these. */
#ifndef GABLE_RT_INTERNAL_H
#define GABLE_RT_INTERNAL_H

#include <stdint.h>

#include "gable/rt.h"

/* Allocates `bytes` (at least 1) of reclaimable memory, or terminates the
 * program. The collector may run during this call, so callers must keep every
 * value they still need reachable from their own stack frame.
 *
 * Memory from here is never freed by the caller: freeing it would return a
 * pointer that is not a malloc block, and the collector would free it again
 * later. Drop the reference and let the collector reclaim it. */
void *gable_rt_allocate(size_t bytes);

/* The base of the running thread's stack, or NULL when the host cannot report
 * it (the collector then does not run). Defined in stack_top.c. */
char *gable_rt_stack_top(void);

/* Runs a collection now. Only meaningful once gable_rt_init has been called;
 * used by tests that assert on live memory. */
void gable_rt_gc_collect(void);

/* Counters used by the memory tests. */
typedef struct {
  unsigned long long collections;
  unsigned long long allocations;
  size_t live_bytes;
  size_t live_blocks;
} gable_rt_GcStats;

gable_rt_GcStats gable_rt_gc_stats(void);

/* Copies `len` bytes. */
gable_str gable_rt_str_copy(const char *data, int64_t len);

bool gable_rt_is_space(char c);

/* Writes `len` bytes to the given stream, followed by a newline. */
void gable_rt_write(gable_str value, bool to_stderr, bool newline);

#endif /* GABLE_RT_INTERNAL_H */

/* Gable runtime -- reclamation of heap values.
 *
 * Strings and lists that outlive the expression that created them live on a
 * heap that this file owns. The collector is a conservative mark-and-sweep: the
 * roots are the machine stack and the registers, and every live block is scanned
 * word by word, so neither generated code nor the rest of the runtime has to
 * describe its data. A word is treated as a pointer when it points inside a live
 * block, which can keep an unreachable block alive until a later cycle, but can
 * never free a block that is still reachable.
 *
 * The design is chosen for exactly that asymmetry. Reclaiming too little costs
 * memory; reclaiming too much corrupts a running program. It also keeps the
 * language free of ownership annotations: a program gets reclamation without
 * saying anything about it, and value semantics stay as specified.
 *
 * Collection happens when a block is allocated and the bytes allocated since the
 * last cycle exceed a threshold that grows with the live set, so the cost is
 * proportional to the live data and amortised over allocation. Nothing runs on a
 * timer and no thread is created.
 *
 * Limits, all deliberate:
 *   - Programs are single-threaded, which the language currently guarantees: no
 *     source construct can start a thread. A second thread would have a second
 *     stack that this scan does not see, so threads cannot be added to the
 *     language without extending the collector first.
 *   - A block is kept alive by any word that looks like a pointer into it, so
 *     peak memory can exceed the exact live set. The overshoot is bounded by the
 *     amount of stale pointer-shaped data on the stack between collections.
 *   - The stack region comes from the host (stack_top.c). Every frame that a
 *     Gable program can reach lives in that region, so nothing is missed; a host
 *     that cannot report the region gets no collection rather than a guess.
 */
#include "gable/rt.h"

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "rt_internal.h"

/* AddressSanitizer poisons the redzones between stack variables, and a
 * conservative scan walks straight through them. Reading a poisoned word would
 * be reported as an overflow, so the scan asks the sanitizer first and skips the
 * word. Nothing live is skipped: a whole pointer lives inside one variable, so
 * its word is fully addressable, and a word that is not fully addressable is not
 * a pointer the program can use either. */
#if defined(__SANITIZE_ADDRESS__)
#define GABLE_HAVE_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GABLE_HAVE_ASAN 1
#endif
#endif

#ifdef GABLE_HAVE_ASAN
/* Declared rather than included: sanitizer/asan_interface.h is not installed on
 * every host that can build the runtime. */
extern void *__asan_region_is_poisoned(void *begin, size_t size);
#endif

/* Payload bytes that may be allocated before the first collection, and the floor
 * for every later threshold. Small enough that a temporary-heavy loop is
 * collected often, large enough that allocation is not dominated by scanning. */
#define GABLE_GC_MIN_THRESHOLD ((size_t)1024 * 1024)

/* One header per heap block. The union forces the payload that follows it to the
 * alignment `max_align_t` requires, so a list of any Gable type is correctly
 * aligned. */
typedef union GableHeapHeader {
  struct {
    size_t size; /* payload bytes */
    size_t mark; /* 1 while the block is reachable in the current cycle */
  } fields;
  /* The payload starts after this header, so the union forces it to be aligned
   * for every kind of value the language stores. `max_align_t` would say this in
   * one word, but MSVC does not provide it in C mode, and these members cover the
   * same requirement: pointers, the widest integer, and the widest floating type
   * the host has. */
  void *as_pointer;
  int64_t as_integer;
  uint64_t as_unsigned;
  long double as_long_double;
} GableHeapHeader;

/* Live blocks, sorted by address so that a candidate pointer can be resolved
 * with one binary search. */
static GableHeapHeader **g_blocks = NULL;
static size_t g_blockCount = 0;
static size_t g_blockCapacity = 0;

static size_t g_liveBytes = 0;
static size_t g_bytesSinceCollection = 0;
static size_t g_threshold = GABLE_GC_MIN_THRESHOLD;

/* Set by gable_rt_init from the generated entry point. Collection stays off
 * until then, because without a stack top every root would look unreachable: a
 * runtime used from a C test without initialisation leaks instead of corrupting
 * memory. It also stays off on a host that cannot report the thread's stack
 * bounds (see stack_top.c), because a guessed bound could free live memory. */
static char *g_stackTop = NULL;
static bool g_enabled = false;

static GableHeapHeader **g_worklist = NULL;
static size_t g_worklistCount = 0;
static size_t g_worklistCapacity = 0;

static unsigned long long g_collections = 0;
static unsigned long long g_allocations = 0;

/* ------------------------------------------------------------------ table -- */

static void insert_block(GableHeapHeader *block) {
  if (g_blockCount == g_blockCapacity) {
    const size_t capacity = g_blockCapacity == 0 ? 256 : g_blockCapacity * 2;
    GableHeapHeader **grown =
        (GableHeapHeader **)realloc(g_blocks, capacity * sizeof *grown);
    if (grown == NULL) {
      gable_rt_trap("out of memory");
    }
    g_blocks = grown;
    g_blockCapacity = capacity;
  }
  size_t low = 0;
  size_t high = g_blockCount;
  while (low < high) {
    const size_t middle = low + (high - low) / 2;
    if ((uintptr_t)g_blocks[middle] < (uintptr_t)block) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  if (low < g_blockCount) {
    memmove(&g_blocks[low + 1], &g_blocks[low], (g_blockCount - low) * sizeof *g_blocks);
  }
  g_blocks[low] = block;
  ++g_blockCount;
}

/* The block whose extent contains `address`, if any. An address inside a block's
 * header also resolves to that block, which only ever retains memory. */
static GableHeapHeader *find_block(uintptr_t address) {
  if (address == 0 || g_blockCount == 0) {
    return NULL;
  }
  size_t low = 0;
  size_t high = g_blockCount;
  while (low < high) {
    const size_t middle = low + (high - low) / 2;
    if ((uintptr_t)g_blocks[middle] <= address) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  if (low == 0) {
    return NULL;
  }
  GableHeapHeader *candidate = g_blocks[low - 1];
  const uintptr_t start = (uintptr_t)candidate;
  const uintptr_t end = start + sizeof(GableHeapHeader) + candidate->fields.size;
  return address >= start && address < end ? candidate : NULL;
}

/* ---------------------------------------------------------------- marking -- */

static void push_work(GableHeapHeader *block) {
  if (g_worklistCount == g_worklistCapacity) {
    const size_t capacity = g_worklistCapacity == 0 ? 256 : g_worklistCapacity * 2;
    GableHeapHeader **grown =
        (GableHeapHeader **)realloc(g_worklist, capacity * sizeof *grown);
    if (grown == NULL) {
      gable_rt_trap("out of memory");
    }
    g_worklist = grown;
    g_worklistCapacity = capacity;
  }
  g_worklist[g_worklistCount++] = block;
}

static void mark_address(uintptr_t address) {
  GableHeapHeader *block = find_block(address);
  if (block == NULL || block->fields.mark != 0) {
    return;
  }
  block->fields.mark = 1;
  push_work(block);
}

/* Treats every aligned word in [begin, end) as a possible pointer. The words are
 * read with memcpy so that an unaligned or partly initialised range is defined
 * behaviour, and the comparison is done as an integer, so a stale word cannot
 * cause a dereference. */
static void mark_range(const void *begin, const void *end) {
  const uintptr_t low = (uintptr_t)begin;
  const uintptr_t high = (uintptr_t)end;
  if (low >= high) {
    return;
  }
  uintptr_t address = low;
  const uintptr_t alignment = sizeof(uintptr_t);
  if (address % alignment != 0) {
    address += alignment - address % alignment;
  }
  for (; address <= high - alignment; address += alignment) {
#ifdef GABLE_HAVE_ASAN
    if (__asan_region_is_poisoned((void *)address, sizeof(uintptr_t)) != NULL) {
      continue;
    }
#endif
    uintptr_t word = 0;
    memcpy(&word, (const void *)address, sizeof word);
    mark_address(word);
  }
}

static void mark_stack(void) {
  char probe = 0;
  const char *bottom = &probe;
  const char *top = g_stackTop;
  if (top == NULL) {
    return;
  }
  /* The stack grows down on every supported host; both orders are handled so
   * that the code does not depend on which way it goes. */
  if (bottom < top) {
    mark_range(bottom, top);
  } else {
    mark_range(top, bottom);
  }
}

/* Callee-saved registers hold live values across a call, and the compiler is free
 * to keep them out of memory. setjmp copies them into the buffer, which is then
 * scanned like stack memory. */
static void mark_registers(void) {
  jmp_buf registers;
  memset(&registers, 0, sizeof registers);
  (void)setjmp(registers);
  mark_range(&registers, (const char *)&registers + sizeof registers);
}

/* ------------------------------------------------------------------ sweep -- */

static void collect(void) {
  ++g_collections;
  g_worklistCount = 0;
  mark_stack();
  mark_registers();
  while (g_worklistCount > 0) {
    const GableHeapHeader *block = g_worklist[--g_worklistCount];
    const char *payload = (const char *)(block + 1);
    mark_range(payload, payload + block->fields.size);
  }

  size_t kept = 0;
  g_liveBytes = 0;
  for (size_t index = 0; index < g_blockCount; ++index) {
    GableHeapHeader *block = g_blocks[index];
    if (block->fields.mark != 0) {
      block->fields.mark = 0;
      g_blocks[kept++] = block;
      g_liveBytes += block->fields.size;
    } else {
      free((void *)block);
    }
  }
  g_blockCount = kept;
  g_bytesSinceCollection = 0;
  /* Collecting again after allocating twice the live set keeps the cost per
   * allocated byte bounded, without scanning a mostly empty heap repeatedly. */
  g_threshold = g_liveBytes > GABLE_GC_MIN_THRESHOLD / 2 ? g_liveBytes * 2
                                                         : GABLE_GC_MIN_THRESHOLD;
}

/* ---------------------------------------------------------------- public -- */

/* Frees every block at process exit. The program has already finished, so no
 * value is used again; this only keeps tools that report leaks (ASan, Valgrind)
 * meaningful for programs that use the runtime. */
static void release_all(void) {
  for (size_t index = 0; index < g_blockCount; ++index) {
    free((void *)g_blocks[index]);
  }
  g_blockCount = 0;
  g_liveBytes = 0;
  free(g_blocks);
  g_blocks = NULL;
  g_blockCapacity = 0;
  free(g_worklist);
  g_worklist = NULL;
  g_worklistCapacity = 0;
  g_enabled = false;
  g_stackTop = NULL;
}

void gable_rt_init(void) {
  if (g_enabled) {
    return;
  }
  g_stackTop = gable_rt_stack_top();
  g_enabled = g_stackTop != NULL;
  atexit(release_all);
}

void *gable_rt_allocate(size_t bytes) {
  if (bytes == 0) {
    bytes = 1;
  }
  if (g_enabled && g_bytesSinceCollection + bytes > g_threshold) {
    collect();
  }
  GableHeapHeader *block = (GableHeapHeader *)malloc(sizeof(GableHeapHeader) + bytes);
  if (block == NULL && g_enabled) {
    collect();
    block = (GableHeapHeader *)malloc(sizeof(GableHeapHeader) + bytes);
  }
  if (block == NULL) {
    gable_rt_trap("out of memory");
  }
  block->fields.size = bytes;
  block->fields.mark = 0;
  insert_block(block);
  ++g_allocations;
  g_liveBytes += bytes;
  g_bytesSinceCollection += bytes;
  return (void *)(block + 1);
}

void gable_rt_gc_collect(void) {
  if (g_enabled) {
    collect();
  }
}

gable_rt_GcStats gable_rt_gc_stats(void) {
  gable_rt_GcStats stats;
  stats.collections = g_collections;
  stats.allocations = g_allocations;
  stats.live_bytes = g_liveBytes;
  stats.live_blocks = g_blockCount;
  return stats;
}

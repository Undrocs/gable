/* Tests for the runtime's memory reclamation.
 *
 * These call the runtime directly rather than through a Gable program, because
 * the properties that matter are about memory rather than output: that live
 * values survive a collection, that unreachable ones are reclaimed, and that a
 * slice into a string keeps the string alive. A compiled Gable program can only
 * show the first of those, and only indirectly.
 *
 * gable_rt_init() is called from main(), which is the same contract the
 * generated entry point honours, so the test observes the collector through the
 * same setup a Gable program gets.
 *
 * Usage: gc_tests
 */
#include <stdio.h>
#include <string.h>

#include "gable/rt.h"
#include "rt_internal.h"

static int failures = 0;
static int checks = 0;

static void fail(const char* name, const char* message) {
  ++failures;
  fprintf(stderr, "FAIL %s\n    %s\n", name, message);
}

static void check(int condition, const char* name, const char* message) {
  ++checks;
  if (!condition) {
    fail(name, message);
  }
}

static void checkBelow(size_t actual, size_t limit, const char* name, const char* what) {
  ++checks;
  if (actual > limit) {
    char message[160];
    snprintf(message, sizeof(message), "%s: expected at most %zu, got %zu", what, limit, actual);
    fail(name, message);
  }
}

/* Allocates blocks and drops every reference to them, so that only the caller's
 * frame (which has already returned) could still mention them. */
static void allocate_and_forget(size_t blocks, size_t bytes) {
  for (size_t index = 0; index < blocks; ++index) {
    unsigned char* block = (unsigned char*)gable_rt_allocate(bytes);
    block[0] = (unsigned char)index;
    block[bytes - 1] = (unsigned char)(index + 1);
  }
}

/* Keeps a prefix of a block alive through an interior pointer, the way a string
 * slice does, and returns it to the caller. */
static unsigned char* keep_interior(size_t bytes) {
  unsigned char* block = (unsigned char*)gable_rt_allocate(bytes);
  for (size_t index = 0; index < bytes; ++index) {
    block[index] = (unsigned char)('a' + index % 26);
  }
  return block + bytes / 2;
}

static void test_before_init(void) {
  /* A runtime that was never initialised has no stack bottom, so it must not
   * collect: guessing would free live memory. */
  gable_rt_gc_collect();
  const gable_rt_GcStats stats = gable_rt_gc_stats();
  check(stats.collections == 0, "before_init", "a collection ran before gable_rt_init");
}

static void test_live_values_survive(void) {
  unsigned char* blocks[16];
  for (size_t index = 0; index < 16; ++index) {
    blocks[index] = (unsigned char*)gable_rt_allocate(4096);
    memset(blocks[index], (int)('A' + index), 4096);
  }
  gable_rt_gc_collect();
  gable_rt_gc_collect();

  int intact = 1;
  for (size_t index = 0; index < 16; ++index) {
    for (size_t offset = 0; offset < 4096; offset += 997) {
      if (blocks[index][offset] != (unsigned char)('A' + index)) {
        intact = 0;
      }
    }
  }
  check(intact, "live_survives", "a reachable block was changed or reclaimed");

  const gable_rt_GcStats stats = gable_rt_gc_stats();
  check(stats.live_blocks >= 16, "live_survives", "live blocks were not reported after collection");
}

static void test_unreachable_is_reclaimed(void) {
  allocate_and_forget(4000, 1024);
  gable_rt_gc_collect();
  const gable_rt_GcStats stats = gable_rt_gc_stats();
  /* A few blocks can survive because a stale word on the stack still looks like
   * a pointer into them; that is the documented overshoot. Four thousand blocks
   * must not survive, which is what a runtime that never frees would report. */
  checkBelow(stats.live_blocks, 64, "unreachable_reclaimed", "live blocks");
  checkBelow(stats.live_bytes, 256 * 1024, "unreachable_reclaimed", "live bytes");
}

static void test_automatic_collection(void) {
  const unsigned long long before = gable_rt_gc_stats().collections;
  /* Eight megabytes of short-lived blocks: far above the one megabyte floor, so
   * allocation alone must trigger collections without any explicit call. */
  allocate_and_forget(8192, 1024);
  const gable_rt_GcStats during = gable_rt_gc_stats();
  check(during.collections > before, "automatic_collection",
        "allocating short-lived blocks did not trigger a collection");
  checkBelow(during.live_bytes, 4 * 1024 * 1024, "automatic_collection", "live bytes while allocating");

  gable_rt_gc_collect();
  checkBelow(gable_rt_gc_stats().live_bytes, 256 * 1024, "automatic_collection",
             "live bytes after the allocations were dropped");
}

static void test_interior_pointer_retains_block(void) {
  unsigned char* middle = keep_interior(2048);
  gable_rt_gc_collect();
  gable_rt_gc_collect();
  /* Offset 0 of the slice is byte 1024 of the block: 'a' + 1024 % 26 = 'a' + 10. */
  check(*middle == (unsigned char)('a' + 1024 % 26), "interior_pointer",
        "the block behind an interior pointer was reclaimed or overwritten");
}

static void test_heap_chain(void) {
  /* A heap block that references other heap blocks, as a list of strings does.
   * Only the head is on the stack. */
  unsigned char** chain = (unsigned char**)gable_rt_allocate(128 * sizeof(unsigned char*));
  for (size_t index = 0; index < 128; ++index) {
    chain[index] = (unsigned char*)gable_rt_allocate(64);
    memset(chain[index], (int)(index + 1), 64);
  }
  gable_rt_gc_collect();
  int intact = 1;
  for (size_t index = 0; index < 128; ++index) {
    if (chain[index][0] != (unsigned char)(index + 1) || chain[index][63] != (unsigned char)(index + 1)) {
      intact = 0;
    }
  }
  check(intact, "heap_chain", "a block reachable through the heap was reclaimed");
}

static void test_string_growth_survives_collection(void) {
  /* Build a string with the real string API, so the collector also sees the
   * values the language itself creates. Concatenation allocates a fresh block
   * each time; only the newest result is reachable when the next one is built. */
  gable_str text = gable_rt_str_copy("", 0);
  for (int index = 0; index < 2000; ++index) {
    const gable_str piece = gable_rt_str_copy("abc", 3);
    text = gable_rt_str_concat(text, piece);
  }
  gable_rt_gc_collect();
  check(text.len == 6000, "string_growth", "the retained string lost bytes across a collection");
  int intact = 1;
  for (int64_t index = 0; index < text.len; ++index) {
    if (text.data[index] != "abc"[index % 3]) {
      intact = 0;
    }
  }
  check(intact, "string_growth", "the retained string was overwritten");
  /* The two thousand intermediates (about 18 MB in total) must be gone, leaving
   * the 6000-byte result and whatever stale pointer-shaped words the optimiser
   * left on the stack. The bound is generous on purpose: a conservative
   * collector may keep a few blocks, but it cannot keep eighteen megabytes. */
  checkBelow(gable_rt_gc_stats().live_bytes, 256 * 1024, "string_growth",
             "live bytes after building one long string");
}

static void test_many_small_allocations(void) {
  for (int index = 0; index < 200000; ++index) {
    char* block = (char*)gable_rt_allocate(16);
    block[0] = (char)index;
  }
  gable_rt_gc_collect();
  const gable_rt_GcStats stats = gable_rt_gc_stats();
  check(stats.allocations >= 200000, "many_small_allocations", "allocations were not counted");
  checkBelow(stats.live_blocks, 256, "many_small_allocations", "live blocks");
}

int main(void) {
  test_before_init();
  gable_rt_init();

  test_live_values_survive();
  test_unreachable_is_reclaimed();
  test_automatic_collection();
  test_interior_pointer_retains_block();
  test_heap_chain();
  test_string_growth_survives_collection();
  test_many_small_allocations();

  if (failures == 0) {
    printf("PASS %d/%d runtime memory tests\n", checks, checks);
    return 0;
  }
  printf("FAILED %d/%d runtime memory tests\n", checks - failures, checks);
  return 1;
}

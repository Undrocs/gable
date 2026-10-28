/* Tests for growable lists in the runtime.
 *
 * The integration suite runs Gable programs that use lists, which covers the
 * language surface. These checks reach the parts a program cannot show: that
 * growth preserves the elements, that a list is a handle shared by every copy,
 * that an element is stored at the stride the element type asks for, and that a
 * list that is no longer reachable is reclaimed.
 *
 * Usage: list_tests
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

static void checkEqual(long long actual, long long expected, const char* name, const char* what) {
  ++checks;
  if (actual != expected) {
    char message[160];
    snprintf(message, sizeof(message), "%s: expected %lld, got %lld", what, expected, actual);
    fail(name, message);
  }
}

struct point {
  int32_t x;
  int32_t y;
};

/* Allocates a list and drops the only handle to it. */
static void abandon_lists(int count) {
  for (int index = 0; index < count; ++index) {
    gable_list list = gable_rt_list_new((int64_t)sizeof(int64_t));
    for (int item = 0; item < 32; ++item) {
      const int64_t value = item;
      gable_rt_list_push(list, &value);
    }
  }
}

static void test_empty_list(void) {
  gable_list list = gable_rt_list_new((int64_t)sizeof(int32_t));
  checkEqual(gable_rt_list_len(list), 0, "empty_list", "length");
  check(list->data == NULL, "empty_list", "an empty list should not allocate storage yet");
}

static void test_growth_preserves_elements(void) {
  /* 1000 elements crosses several doublings of the capacity. */
  gable_list list = gable_rt_list_new((int64_t)sizeof(int32_t));
  for (int32_t value = 0; value < 1000; ++value) {
    gable_rt_list_push(list, &value);
  }
  checkEqual(gable_rt_list_len(list), 1000, "growth", "length");
  int intact = 1;
  for (int64_t index = 0; index < 1000; ++index) {
    int32_t value = 0;
    memcpy(&value, gable_rt_list_at(list, index), sizeof value);
    if (value != (int32_t)index) {
      intact = 0;
    }
  }
  check(intact, "growth", "elements were lost or reordered while the list grew");
  check(list->capacity >= list->len, "growth", "capacity must hold the elements");
}

static void test_handle_is_shared(void) {
  gable_list first = gable_rt_list_new((int64_t)sizeof(int64_t));
  gable_list second = first;
  int64_t value = 42;
  gable_rt_list_push(second, &value);
  checkEqual(gable_rt_list_len(first), 1, "shared_handle", "length seen through the first handle");
  int64_t read = 0;
  memcpy(&read, gable_rt_list_at(first, 0), sizeof read);
  checkEqual(read, 42, "shared_handle", "value seen through the first handle");
}

static void test_element_stride(void) {
  gable_list points = gable_rt_list_new((int64_t)sizeof(struct point));
  const struct point first = {1, 2};
  const struct point second = {3, 4};
  gable_rt_list_push(points, &first);
  gable_rt_list_push(points, &second);
  struct point read = {0, 0};
  memcpy(&read, gable_rt_list_at(points, 1), sizeof read);
  checkEqual(read.x, 3, "element_stride", "x of the second point");
  checkEqual(read.y, 4, "element_stride", "y of the second point");
}

static void test_abandoned_lists_are_reclaimed(void) {
  abandon_lists(2000);
  gable_rt_gc_collect();
  const gable_rt_GcStats stats = gable_rt_gc_stats();
  /* Each abandoned list was a handle plus storage for 32 elements; a few may
   * survive through stale stack words, 2000 of them must not. */
  check(stats.live_blocks < 256, "reclaimed", "abandoned lists were not reclaimed");
}

int main(void) {
  gable_rt_init();

  test_empty_list();
  test_growth_preserves_elements();
  test_handle_is_shared();
  test_element_stride();
  test_abandoned_lists_are_reclaimed();

  if (failures == 0) {
    printf("PASS %d/%d runtime list tests\n", checks, checks);
    return 0;
  }
  printf("FAILED %d/%d runtime list tests\n", checks - failures, checks);
  return 1;
}

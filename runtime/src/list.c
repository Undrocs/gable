/* Gable runtime -- growable lists.
 *
 * A list is a handle to a block of storage that holds `len` elements of
 * `stride` bytes each. Growing allocates a larger block and copies the elements
 * into it; the old block becomes garbage and the collector reclaims it. The
 * handle itself is a heap block too, so a list can outlive the frame that
 * created it.
 *
 * One implementation serves every element type because the element size is
 * stored in the handle. The generated code passes addresses of elements and
 * reads them back with memcpy, so an element may be any type the language has,
 * including a struct or another list.
 */
#include "gable/rt.h"

#include <stdint.h>
#include <string.h>

#include "rt_internal.h"

/* Elements the first allocation of a list holds. Growing doubles the capacity,
 * so appending n elements copies about 2n of them in total. */
#define GABLE_LIST_INITIAL_CAPACITY 8

/* Largest number of elements a list may hold. The bound keeps the byte count
 * from overflowing a size_t on any host we build for. */
#define GABLE_LIST_MAX_ELEMENTS ((int64_t)1 << 40)

static void fail_too_large(void) { gable_rt_trap("list is too large"); }

/* Ensures `list` can hold one more element. */
static void reserve_one(gable_list list) {
  if (list->len < list->capacity) {
    return;
  }
  int64_t capacity = list->capacity == 0 ? GABLE_LIST_INITIAL_CAPACITY : list->capacity * 2;
  if (capacity > GABLE_LIST_MAX_ELEMENTS || capacity > (int64_t)(SIZE_MAX / (size_t)list->stride)) {
    fail_too_large();
  }
  void *data = gable_rt_allocate((size_t)capacity * (size_t)list->stride);
  if (list->len > 0) {
    memcpy(data, list->data, (size_t)list->len * (size_t)list->stride);
  }
  list->data = data;
  list->capacity = capacity;
}

gable_list gable_rt_list_new(int64_t element_size) {
  if (element_size <= 0) {
    gable_rt_trap("a list needs a positive element size");
  }
  gable_list list = (gable_list)gable_rt_allocate(sizeof *list);
  list->data = NULL;
  list->len = 0;
  list->capacity = 0;
  list->stride = element_size;
  return list;
}

void gable_rt_list_push(gable_list list, const void *element) {
  reserve_one(list);
  memcpy((char *)list->data + (size_t)list->len * (size_t)list->stride, element,
         (size_t)list->stride);
  list->len += 1;
}

void *gable_rt_list_at(gable_list list, int64_t index) {
  if (index < 0 || index >= list->len) {
    gable_rt_index_error(index, list->len);
  }
  return (char *)list->data + (size_t)index * (size_t)list->stride;
}

int64_t gable_rt_list_len(gable_list list) { return list->len; }

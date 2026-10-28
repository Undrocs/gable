/* Gable runtime -- where the running thread's stack begins.
 *
 * The collector finds the values that are still in use by scanning the machine
 * stack from the current stack pointer up to the base of the thread's stack. The
 * upper bound cannot be taken from the address of a local variable: the compiler
 * is free to place that variable anywhere in the frame (AddressSanitizer, for
 * one, reorders locals and keeps values that no longer matter in place), and a
 * bound that is too low would let the collector free a value that is still
 * reachable. Each host is asked for the thread's stack bounds instead.
 *
 * When a host cannot report them, gable_rt_stack_top returns NULL and the
 * collector stays off: memory is then not reclaimed, which is safe, rather than
 * being reclaimed wrongly, which is not.
 */
#if defined(_WIN32)
/* GetCurrentThreadStackLimits is a Windows 8 API. Asking for it explicitly keeps
 * the declaration visible even when the build does not set a target version. */
#if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0602)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#include <windows.h>
#else
#if defined(__APPLE__)
#include <pthread.h>
#else
/* pthread_getattr_np is a GNU extension. */
#define _GNU_SOURCE 1
#include <pthread.h>
#endif
#endif

char *gable_rt_stack_top(void);

#if defined(_WIN32)

char *gable_rt_stack_top(void) {
  ULONG_PTR low = 0;
  ULONG_PTR high = 0;
  GetCurrentThreadStackLimits(&low, &high);
  return (char *)high;
}

#elif defined(__APPLE__)

char *gable_rt_stack_top(void) {
  return (char *)pthread_get_stackaddr_np(pthread_self());
}

#else

char *gable_rt_stack_top(void) {
  pthread_attr_t attributes;
  if (pthread_getattr_np(pthread_self(), &attributes) != 0) {
    return NULL;
  }
  void *base = NULL;
  size_t size = 0;
  char *top = NULL;
  if (pthread_attr_getstack(&attributes, &base, &size) == 0 && base != NULL) {
    top = (char *)base + size;
  }
  pthread_attr_destroy(&attributes);
  return top;
}

#endif

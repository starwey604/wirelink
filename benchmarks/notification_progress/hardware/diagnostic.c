/* SPDX-License-Identifier: Apache-2.0 */
#include "diagnostic.h"
#include <stdatomic.h>
#include <stdio.h>
#include <zephyr/kernel.h>
static atomic_uint_least32_t counts[D_COUNT];
static uint32_t first[D_COUNT], delta[D_COUNT];
#define DIAG_NAME(name) #name,
static const char *const names[] = { DIAG_EVENTS(DIAG_NAME) };
void diag_add(enum diag_event event, uint32_t amount) {
  atomic_fetch_add_explicit(&counts[event], amount, memory_order_relaxed);
}
void diag_start(void) {
  unsigned key = irq_lock();
  for (unsigned i = 0; i < D_COUNT; ++i) {
    __ASSERT(atomic_is_lock_free(&counts[i]), "diagnostic requires lock-free counters");
    first[i] = atomic_load_explicit(&counts[i], memory_order_relaxed);
  }
  irq_unlock(key);
}
void diag_stop(void) {
  unsigned key = irq_lock();
  for (unsigned i = 0; i < D_COUNT; ++i)
    delta[i] = atomic_load_explicit(&counts[i], memory_order_relaxed) - first[i];
  irq_unlock(key);
}
size_t diag_json(char *data, size_t capacity, unsigned page) {
  unsigned begin = page * 8U, end = begin + 8U;
  if (begin >= D_COUNT) return 0U;
  if (end > D_COUNT) end = D_COUNT;
  int n = snprintf(data, capacity, "{\"page\":%u,\"counts\":{", page);
  if (n < 0 || (size_t)n >= capacity) return 0U;
  size_t used = (size_t)n;
  for (unsigned i = begin; i < end; ++i) {
    n = snprintf(data + used, capacity - used, "%s\"%s\":%u", i == begin ? "" : ",", names[i], delta[i]);
    if (n < 0 || (size_t)n >= capacity - used) return 0U;
    used += (size_t)n;
  }
  n = snprintf(data + used, capacity - used, "},\"done\":%s}", end == D_COUNT ? "true" : "false");
  return n < 0 || (size_t)n >= capacity - used ? 0U : used + (size_t)n;
}

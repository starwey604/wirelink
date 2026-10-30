/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_IO_READINESS_H_
#define WIRELINK_IO_READINESS_H_
#include <stdint.h>
/* Adapter-only relative wait hint. No protocol state, clock or scheduler. */
typedef struct {
  uint32_t runnable, next_deadline_ms, blocked;
} wl_io_readiness_t;
static inline wl_io_readiness_t wl_io_readiness_idle(void) {
  wl_io_readiness_t value = {0U, UINT32_MAX, 0U};
  return value;
}
static inline wl_io_readiness_t wl_io_readiness_at(uint32_t delay) {
  wl_io_readiness_t value = {delay == 0U, delay, delay != UINT32_MAX};
  return value;
}
static inline void wl_io_readiness_merge(wl_io_readiness_t *value, wl_io_readiness_t other) {
  value->runnable |= other.runnable;
  value->blocked |= other.blocked;
  if (other.next_deadline_ms < value->next_deadline_ms)
    value->next_deadline_ms = other.next_deadline_ms;
}
#endif

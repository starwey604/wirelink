/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_TEST_ENVIRONMENT_H
#define WIRELINK_TEST_ENVIRONMENT_H
#include "wirelink/environment.h"
/* Deterministic single-thread test source, not a boot-unique production source. */
static inline wl_err_t test_next_session(void *context, uint64_t *out) {
  static uint64_t next = 100U;
  (void)context;
  if (next == UINT64_MAX) return WL_ERR_INVALID_STATE;
  *out = ++next;
  return WL_OK;
}
static inline wl_environment_t test_environment_id(uint64_t valid, wl_clock_t clock) {
  wl_environment_t environment = {clock, {NULL, NULL}};
  if (valid != 0U) environment.session.next = test_next_session;
  return environment;
}
#endif

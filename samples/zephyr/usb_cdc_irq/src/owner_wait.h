/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_SAMPLE_CDC_OWNER_WAIT_H
#define WIRELINK_SAMPLE_CDC_OWNER_WAIT_H

#include "wirelink/endpoint.h"

/* Sample-local owner policy after a bounded step, shared with its integration
 * tests. The endpoint owns the clock domain; the attached waiter owns blocking,
 * notification consumption and stop. This does not step or dispatch protocol events. */
static inline wl_err_t cdc_wait_for_activity(wl_endpoint_t *endpoint) {
  wl_time_ms_t queried_at, wait_now;
  wl_readiness_t hint;
  wl_err_t result = wl_endpoint_now(endpoint, &queried_at);
  if (result != WL_OK) return result;
  result = wl_endpoint_get_readiness_at(endpoint, queried_at, &hint);
  if (result != WL_OK) return result;
  const wl_waiter_t *waiter = wl_endpoint_waiter(endpoint);
  if (waiter == NULL) return WL_ERR_NOT_SUPPORTED;
  uint32_t remaining = hint.runnable ? 0U : hint.next_deadline_ms;
  if (remaining != 0U && remaining != WL_NO_DEADLINE) {
    result = wl_endpoint_now(endpoint, &wait_now);
    if (result != WL_OK) return result;
    remaining = wl_wait_remaining(remaining, queried_at, wait_now);
  }
  /* A zero-time wait also observes a stop during the query. Consume the latch
   * only in wait; the caller always performs the next step after a wake. */
  return waiter->wait(waiter->user_data, remaining);
}

#endif

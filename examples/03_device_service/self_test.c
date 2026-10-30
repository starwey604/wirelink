/* SPDX-License-Identifier: Apache-2.0 */
#include "self_test.h"
#include "loopback_owner.h"
static int32_t immediate(void *user, const self_test_request_t *request,
                         self_test_response_value_t *response) {
  (void)user;
  response->has_observed = true;
  response->observed = request->pattern ^ UINT32_C(0xa5a5);
  return 0;
}
static int32_t start(void *user, const start_self_test_request_t *request,
                     start_self_test_response_value_t *response) {
  device_service_t *state = user;
  if (state->job_pending || state->next_job == UINT32_MAX) return DEVICE_BUSY;
  state->job_id = ++state->next_job;
  state->pattern = request->pattern;
  state->due = state->now + 20U;
  state->job_pending = true;
  response->has_job_id = true;
  response->job_id = state->job_id;
  return 0;
}
void self_test_bind(device_kernel_handlers_t *handlers) {
  handlers->on_rpc_self_test = immediate;
  handlers->on_rpc_start_self_test = start;
}
/* Application event-loop work, with ordinary TX backpressure. No reply token. */
void self_test_poll(device_service_t *state, device_kernel_t *endpoint) {
  if (!state->job_pending || (int32_t)(state->now - state->due) < 0) return;
  self_test_progress_value_t progress = {.has_job_id = true, .job_id = state->job_id,
    .has_observed = true, .observed = state->pattern ^ UINT32_C(0xa5a5)};
  const wl_err_t error = device_kernel_send_self_test_progress(endpoint, &progress);
  if (error == WL_ERR_WOULD_BLOCK) return;
  EXAMPLE_REQUIRE(error == WL_OK);
  state->job_pending = false;
}

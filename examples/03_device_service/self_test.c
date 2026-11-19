/* SPDX-License-Identifier: Apache-2.0 */
#include "self_test.h"
int32_t device_server_on_self_test(device_server_node_t *self, const self_test_request_t *request,
                         self_test_response_t *response) {
  (void)self;
  response->observed = request->pattern ^ UINT32_C(0xa5a5);
  return 0;
}
/* [docs:device_job_start] */
int32_t device_server_on_start_self_test(device_server_node_t *self, const start_self_test_request_t *request,
                     start_self_test_response_t *response) {
  device_service_t *state = self->user;
  if (state->job_pending || state->next_job == UINT32_MAX) return DEVICE_BUSY;
  state->job_id = ++state->next_job;
  state->pattern = request->pattern;
  state->due = state->now + 20U;
  state->job_pending = true;
  response->job_id = state->job_id;
  return 0;
}
/* [/docs:device_job_start] */
/* Application event-loop work, with ordinary TX backpressure. No reply token. */
/* [docs:device_job_poll] */
wl_err_t self_test_poll(device_service_t *state, device_server_node_t *endpoint) {
  if (!state->job_pending || (int32_t)(state->now - state->due) < 0) return WL_OK;
  self_test_progress_t progress = {.job_id = state->job_id,
    .observed = state->pattern ^ UINT32_C(0xa5a5)};
  const wl_err_t error = device_server_send_self_test_progress(endpoint, &progress);
  if (error == WL_ERR_WOULD_BLOCK) return WL_OK;
  if (error != WL_OK) return error;
  state->job_pending = false;
  return WL_OK;
}
/* [/docs:device_job_poll] */

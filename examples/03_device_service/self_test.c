/* SPDX-License-Identifier: Apache-2.0 */
#include "self_test.h"
#include "device_server_advanced.h"
#include "device_service.h"

static int32_t begin(void *context, const self_test_request_t *request,
                     const device_server_self_test_request_token_t *token,
                     wl_delivery_t delivery) {
  self_test_job_t *job = context;
  (void)delivery;
  if (job->pending)
    return device_server_endpoint_self_test_reject(job->endpoint, token, DEVICE_BUSY);
  job->token = *token;
  job->pattern = request->pattern;
  wl_time_ms_t now;
  if (wl_endpoint_now(device_server_endpoint_handle(job->endpoint), &now) != WL_OK) return WL_RPC_ERR_INVALID_STATE;
  job->due = now + 3U;
  job->pending = true;
  return 0; /* Accepted, not completed. */
}

void self_test_bind(device_server_endpoint_config_t *config, self_test_job_t *job) {
  config->advanced.self_test_request_handler = begin;
  config->advanced.self_test_user_data = job;
}

wl_rpc_err_t self_test_poll(self_test_job_t *job, wl_time_ms_t now) {
  if (!job->pending || (int32_t)(now - job->due) < 0) return WL_RPC_OK;
  job->pending = false;
  self_test_response_t response;
  self_test_response_clear(&response);
  response.has_observed = true;
  response.observed = job->pattern ^ UINT32_C(0xa5a5);
  return device_server_endpoint_self_test_complete(job->endpoint, &job->token, &response);
}

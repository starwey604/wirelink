/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EXAMPLE_SELF_TEST_H
#define EXAMPLE_SELF_TEST_H
#include "device_server_endpoint.h"

/* Only a genuinely deferred operation keeps completion authority. */
typedef struct {
  device_server_endpoint_t *endpoint;
  device_server_self_test_request_token_t token;
  unsigned passes_remaining;
  uint32_t pattern;
} self_test_job_t;

void self_test_bind(device_server_endpoint_config_t *config, self_test_job_t *job);
wl_rpc_err_t self_test_poll(self_test_job_t *job);
#endif

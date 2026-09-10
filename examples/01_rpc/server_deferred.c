/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include "calculator_advanced.h"
#include "tutorial_host.h"

typedef struct {
  calculator_endpoint_t *server;
  bool pending;
  calculator_add_request_token_t token;
  int64_t sum;
} job_t;

static int32_t enqueue(void *context, const add_request_t *request,
                       const calculator_add_request_token_t *token,
                       wl_delivery_t delivery) {
  job_t *job = context;
  (void)delivery;
  if (job->pending)
    return calculator_endpoint_add_reject(job->server, token, 2);
  printf("handling %ld + %ld; response deferred\n",
         (long)request->left, (long)request->right);
  fflush(stdout);
  job->sum = (int64_t)request->left + request->right;
  job->token = *token;
  job->pending = true;
  return 0; /* Accepted locally, not a completed RPC response. */
}

/* The demo job becomes ready on the next owner-loop pass. A real worker
 * publishes a result and wakes this owner; it must not call complete itself. */
static wl_rpc_err_t finish(job_t *job) {
  add_response_t response;
  job->pending = false;
  if (job->sum < INT32_MIN || job->sum > INT32_MAX)
    return calculator_endpoint_add_reject(job->server, &job->token, 1);
  add_response_clear(&response);
  response.has_sum = true;
  response.sum = (int32_t)job->sum;
  return calculator_endpoint_add_complete(job->server, &job->token, &response);
}

int main(int argc, char **argv) {
  static calculator_endpoint_t server;
  calculator_endpoint_config_t config;
  job_t job = {0};
  uint16_t local = 49101, peer = 49100;
  CHECK(example_ports(argc, argv, &local, &peer));
  job.server = &server;
  CHECK(calculator_endpoint_config_defaults(&config, wl_platform_environment()) == WL_OK);

  config.advanced.add_request_handler = enqueue;
  config.advanced.add_user_data = &job;
  CHECK(calculator_endpoint_init_config(&server, &config) == WL_OK);
  example_udp_t *udp = example_udp_open(calculator_endpoint_handle(&server), local, peer);
  CHECK(udp != NULL);
  puts("calculator server ready (deferred)");
  fflush(stdout);
  while (example_running()) {
    if (job.pending) {
      CHECK(finish(&job) == WL_RPC_OK);
      puts("deferred response queued");
      fflush(stdout);
    }
    CHECK(calculator_endpoint_step(&server) == WL_OK);
    if (!job.pending) CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  CHECK(calculator_endpoint_close(&server) == WL_OK);
  job.pending = false; /* A saved token cannot outlive its endpoint. */
  example_udp_close(udp);
  return 0;
}

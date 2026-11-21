/* SPDX-License-Identifier: Apache-2.0 */
#include "service.h"
#include "tutorial_host.h"
#include <inttypes.h>
#include <limits.h>

static unsigned completed;
static wl_rpc_result_t result;
static int32_t sum;

static void added(client_node_t *self, wl_rpc_result_t outcome, const add_response_t *response) {
  (void)self;
  result = outcome;
  if (response != NULL) sum = response->sum;
  ++completed;
}

/* [docs:calculator_rejected_callback] */
static void rejected(client_node_t *self, wl_rpc_result_t outcome, const add_response_t *response) {
  (void)self;
  if (response == NULL) {
    result = outcome;
    ++completed;
  }
}
/* [/docs:calculator_rejected_callback] */

static wl_err_t advance(client_node_t *client, example_udp_t *udp) {
  wl_err_t error = example_udp_step(udp, &client->core, example_now_ms());
  return error == WL_OK ? example_udp_wait(udp, &client->core, 100U) : error;
}

/* [docs:calculator_sync_rpc] */
static wl_err_t add_once(client_node_t *client, example_udp_t *udp, add_request_t request,
                         client_add_cb_t done) {
  completed = 0;
  wl_err_t error = client_call_add(client, &request, done);
  while (error == WL_OK && example_running() && !completed)
    error = advance(client, udp);
  return completed || error != WL_OK ? error : WL_ERR_CANCELLED;
}
/* [/docs:calculator_sync_rpc] */

static int run(client_node_t *client, example_udp_t *udp) {
  if (add_once(client, udp, (add_request_t){.left = 20, .right = 22}, added) != WL_OK ||
      result.outcome != WL_RPC_SUCCESS || sum != 42) return 1;
  printf("20 + 22 = %" PRId32 "\n", sum);

  if (calculator_async_example(client) != WL_OK) return 1;
  for (int count = calculator_async_count(); example_running() && count >= 0 && count < 4;
       count = calculator_async_count()) {
    if (advance(client, udp) != WL_OK) return 1;
  }
  if (calculator_async_count() != 4) return 1;
  puts("4 concurrent calls returned 42");

  if (add_once(client, udp, (add_request_t){.left = INT32_MAX, .right = 1}, rejected) != WL_OK ||
      result.outcome != WL_RPC_REJECTED || result.status != 1) return 1;
  printf("overflow rejected: status=%" PRId32 "\n", result.status);
  return 0;
}

int main(int argc, char **argv) {
  static client_node_t client;
  example_udp_t *udp = example_udp_create(&client.port);
  if (udp == NULL) return 1;
  if (client_init_io(&client, 0U, example_udp_io(udp)) != WL_OK ||
      example_udp_start(udp, CLIENT_TX_WIRE_MAX, argc, argv, 50003, 50004) != WL_OK) {
    example_udp_destroy(udp);
    return 1;
  }
  int status = run(&client, udp);
  if (example_udp_close(udp, &client.core) != WL_OK) status = 1;
  return status;
}

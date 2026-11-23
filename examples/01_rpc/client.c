/* SPDX-License-Identifier: Apache-2.0 */
#include "service.h"
#include "tutorial_host.h"
#include <wirelink/udp.h>
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

/* [docs:calculator_sync_rpc] */
static wl_err_t add_once(client_node_t *client, wl_clock_t clock, add_request_t request,
                         client_add_cb_t done) {
  completed = 0;
  wl_err_t error = client_call_add(client, &request, done);
  while (error == WL_OK && example_running() && !completed)
    error = wl_node_poll(&client->node, clock, 100U);
  return completed || error != WL_OK ? error : WL_ERR_CANCELLED;
}
/* [/docs:calculator_sync_rpc] */

static int run(client_node_t *client) {
  const wl_clock_t clock = example_clock();
  if (add_once(client, clock, (add_request_t){.left = 20, .right = 22}, added) != WL_OK ||
      result.outcome != WL_RPC_SUCCESS || sum != 42) return 1;
  printf("20 + 22 = %" PRId32 "\n", sum);

  if (calculator_async_example(client) != WL_OK) return 1;
  for (int count = calculator_async_count(); example_running() && count >= 0 && count < 4;
       count = calculator_async_count()) {
    if (wl_node_poll(&client->node, clock, 100U) != WL_OK) return 1;
  }
  if (calculator_async_count() != 4) return 1;
  puts("4 concurrent calls returned 42");

  if (add_once(client, clock, (add_request_t){.left = INT32_MAX, .right = 1}, rejected) != WL_OK ||
      result.outcome != WL_RPC_REJECTED || result.status != 1) return 1;
  printf("overflow rejected: status=%" PRId32 "\n", result.status);
  return 0;
}

int main(int argc, char **argv) {
  static client_node_t client;
  wl_udp_config_t config = {
    .bind_address = "127.0.0.1", .bind_port = 50003,
    .peer_address = "127.0.0.1", .peer_port = 50004
  };
  if (!example_udp_args(argc, argv, &config)) return 1;
  wl_udp_t *udp = NULL;
  if (wl_udp_open(&udp, &config, NULL) != WL_OK) return 1;
  if (client_bind(&client, wl_udp_transport(udp)) != WL_OK) {
    wl_udp_close(udp);
    return 1;
  }
  example_install_signals();
  int status = run(&client);
  if (wl_node_close(&client.node, example_now_ms()) != WL_OK) status = 1;
  wl_udp_close(udp);
  return status;
}

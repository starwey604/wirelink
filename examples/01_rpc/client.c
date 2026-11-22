/* SPDX-License-Identifier: Apache-2.0 */
#include "service.h"
#include "tutorial_host.h"
#include <wirelink/asio/packet_udp.h>
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

static wl_err_t advance(client_node_t *client, wl_asio_udp_t *udp) {
  wl_err_t error = wl_asio_udp_step_endpoint(udp, &client->core, example_now_ms());
  return error == WL_OK ? wl_asio_udp_wait_endpoint(udp, &client->core, example_now_ms(), 100U) : error;
}

/* [docs:calculator_sync_rpc] */
static wl_err_t add_once(client_node_t *client, wl_asio_udp_t *udp, add_request_t request,
                         client_add_cb_t done) {
  completed = 0;
  wl_err_t error = client_call_add(client, &request, done);
  while (error == WL_OK && example_running() && !completed)
    error = advance(client, udp);
  return completed || error != WL_OK ? error : WL_ERR_CANCELLED;
}
/* [/docs:calculator_sync_rpc] */

static int run(client_node_t *client, wl_asio_udp_t *udp) {
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
  wl_asio_udp_config_t config = {
    .bind_address = "127.0.0.1", .bind_port = 50003,
    .peer_address = "127.0.0.1", .peer_port = 50004,
    .maximum_tx_datagram_size = CLIENT_TX_WIRE_MAX
  };
  if (!example_udp_args(argc, argv, &config)) return 1;
  wl_asio_udp_t *udp = wl_asio_udp_create(&client.port);
  if (!udp) return 1;
  const wl_packet_io_t io = wl_asio_udp_io(udp);
  if (client_init_io(&client, 0U, &io) != WL_OK ||
      wl_asio_udp_start(udp, &config) != WL_OK) {
    wl_asio_udp_destroy(udp);
    return 1;
  }
  example_install_signals();
  int status = run(&client, udp);
  if (wl_asio_udp_close_endpoint(udp, &client.core, example_now_ms()) != WL_OK) status = 1;
  wl_asio_udp_destroy(udp);
  return status;
}

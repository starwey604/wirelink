/* SPDX-License-Identifier: Apache-2.0 */
#include "business.h"
#include "tutorial_host.h"
#include <wirelink/asio/packet_udp.h>
#include <string.h>
void udp_client_on_telemetry(udp_client_node_t *self, const telemetry_t *message) {
  peer_sample(self->user, message);
}
int main(int argc, char **argv) {
  static udp_client_node_t client;
  peer_client_t state = {0};
  wl_asio_udp_config_t config = {
    .bind_address = "127.0.0.1", .bind_port = 50007,
    .peer_address = "127.0.0.1", .peer_port = 50008,
    .maximum_tx_datagram_size = UDP_CLIENT_TX_WIRE_MAX
  };
  if (!example_udp_args(argc, argv, &config)) return 1;
  wl_asio_udp_t *udp = wl_asio_udp_create(&client.port);
  if (!udp) return 1;
  const wl_packet_io_t io = wl_asio_udp_io(udp);
  uint32_t instance;
  if (example_instance(&instance) != WL_OK ||
      udp_client_init_io(&client, instance, &io) != WL_OK ||
      wl_asio_udp_start(udp, &config) != WL_OK) {
    wl_asio_udp_destroy(udp);
    return 1;
  }
  example_install_signals();
  client.user = &state;
  add_request_t request = {.left = 20, .right = 22};
  const wl_err_t submitted = udp_client_call_add_ex(&client, &request, example_now_ms(), 1500,
                                                  peer_completed, &state, NULL);
  memset(&request, 0, sizeof(request));
  int status = 1;
  while (submitted == WL_OK && example_running() && !state.completions) {
    if (wl_asio_udp_step_endpoint(udp, &client.core, example_now_ms()) != WL_OK ||
        wl_asio_udp_wait_endpoint(udp, &client.core, example_now_ms(), 100) != WL_OK) break;
  }
  if (state.completions && state.result.outcome == WL_RPC_SUCCESS) {
    printf("20 + 22 = %ld\n", (long)state.response.sum);
    const uint32_t started = example_now_ms();
    while (example_running() && !state.samples && (uint32_t)(example_now_ms() - started) < 500) {
      if (wl_asio_udp_step_endpoint(udp, &client.core, example_now_ms()) != WL_OK ||
          wl_asio_udp_wait_endpoint(udp, &client.core, example_now_ms(), 100) != WL_OK) break;
    }
    if (state.samples) { printf("telemetry sample=%lu\n", (unsigned long)state.sample); status = state.response.sum == 42 ? 0 : 1; }
  } else fprintf(stderr, "RPC: outcome=%d status=%d\n", (int)state.result.outcome, state.result.status);
  if (wl_asio_udp_close_endpoint(udp, &client.core, example_now_ms()) != WL_OK) status = 1;
  wl_asio_udp_destroy(udp);
  return status;
}

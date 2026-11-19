/* SPDX-License-Identifier: Apache-2.0 */
#include "business.h"
#include "peer_options.h"
#include <string.h>
void udp_client_on_telemetry(udp_client_node_t *self, const telemetry_t *message) {
  peer_sample(self->user, message);
}
int main(int argc, char **argv) {
  static udp_client_node_t client;
  static wl_packet_port_t port;
  static uint8_t rx[2][2053];
  peer_client_t state = {0};
  example_udp_t *udp = peer_open(&port, (wl_span_t){&rx[0][0], sizeof(rx)}, sizeof(rx[0]), argc, argv);
  CHECK(udp);
  CHECK(udp_client_init(&client, &port) == WL_OK);
  client.user = &state;
  add_request_t request = {.left = 20, .right = 22};
  CHECK(udp_client_call_add_ex(&client, &request, example_now_ms(), 1500, peer_completed, &state, NULL) == WL_OK);
  memset(&request, 0, sizeof(request));
  int status = 1;
  while (example_running() && !state.completions) {
    if (example_udp_step(udp, &client.core, example_now_ms()) != WL_OK ||
        example_udp_wait(udp, &client.core, 100) != WL_OK) break;
  }
  if (state.completions && state.result.outcome == WL_RPC_SUCCESS) {
    printf("20 + 22 = %ld\n", (long)state.response.sum);
    const uint32_t started = example_now_ms();
    while (example_running() && !state.samples && (uint32_t)(example_now_ms() - started) < 500) {
      if (example_udp_step(udp, &client.core, example_now_ms()) != WL_OK ||
          example_udp_wait(udp, &client.core, 100) != WL_OK) break;
    }
    if (state.samples) { printf("telemetry sample=%lu\n", (unsigned long)state.sample); status = state.response.sum == 42 ? 0 : 1; }
  } else fprintf(stderr, "RPC: outcome=%d status=%d\n", (int)state.result.outcome, state.result.status);
  if (example_udp_close(udp, &client.core) != WL_OK) status = 1;
  return status;
}

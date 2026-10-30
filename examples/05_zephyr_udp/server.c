/* SPDX-License-Identifier: Apache-2.0 */
#include "business.h"
#include "peer_options.h"
int main(int argc, char **argv) {
  static udp_demo_kernel_t server;
  static wl_packet_port_t port;
  static uint8_t rx[2][2053];
  example_udp_t *udp = peer_open(&port, (wl_span_t){&rx[0][0], sizeof(rx)}, sizeof(rx[0]), argc, argv);
  CHECK(udp);
  const udp_demo_kernel_handlers_t handlers = {.on_rpc_add = peer_add};
  CHECK(udp_demo_kernel_init(&server, &port, &handlers, NULL, NULL) == WL_OK);
  puts("UDP peer server ready"); fflush(stdout);
  uint32_t sequence = 0, due = example_now_ms();
  int status = 0;
  while (example_running()) {
    uint32_t now = example_now_ms();
    if (example_udp_step(udp, &server.core, now) != WL_OK) { status = 1; break; }
    if ((int32_t)(now - due) >= 0) {
      telemetry_value_t sample = {.has_sample = true, .sample = ++sequence};
      const wl_err_t error = udp_demo_kernel_send_telemetry(&server, &sample);
      if (error != WL_OK && error != WL_ERR_WOULD_BLOCK) { status = 1; break; }
      due = now + 20;
    }
    now = example_now_ms();
    uint32_t wait = (int32_t)(due - now) > 0 ? due - now : 0;
    if (example_udp_wait(udp, &server.core, wait) != WL_OK) { status = 1; break; }
  }
  if (example_udp_close(udp, &server.core) != WL_OK) status = 1;
  return status;
}

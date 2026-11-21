/* SPDX-License-Identifier: Apache-2.0 */
#include "business.h"
#include "tutorial_host.h"
int32_t udp_server_on_add(udp_server_node_t *self, const add_request_t *request, add_response_t *response) {
  return peer_add(self->user, request, response);
}
int main(int argc, char **argv) {
  static udp_server_node_t server;
  example_udp_t *udp = example_udp_create(&server.port);
  CHECK(udp);
  uint32_t instance;
  if (example_instance(&instance) != WL_OK ||
      udp_server_init_io(&server, instance, example_udp_io(udp)) != WL_OK ||
      example_udp_start(udp, UDP_SERVER_TX_WIRE_MAX, argc, argv, 50008, 50007) != WL_OK) {
    example_udp_destroy(udp);
    return 1;
  }
  puts("UDP peer server ready"); fflush(stdout);
  uint32_t sequence = 0, due = example_now_ms();
  int status = 0;
  /* [docs:udp_server_loop] */
  while (example_running()) {
    uint32_t now = example_now_ms();
    if (example_udp_step(udp, &server.core, now) != WL_OK) { status = 1; break; }
    if ((int32_t)(now - due) >= 0) {
      telemetry_t sample = {.sample = ++sequence};
      const wl_err_t error = udp_server_send_telemetry(&server, &sample);
      if (error != WL_OK && error != WL_ERR_WOULD_BLOCK) { status = 1; break; }
      due = now + 20;
    }
    now = example_now_ms();
    uint32_t wait = (int32_t)(due - now) > 0 ? due - now : 0;
    if (example_udp_wait(udp, &server.core, wait) != WL_OK) { status = 1; break; }
  }
  /* [/docs:udp_server_loop] */
  if (example_udp_close(udp, &server.core) != WL_OK) status = 1;
  return status;
}

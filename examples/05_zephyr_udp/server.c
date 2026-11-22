/* SPDX-License-Identifier: Apache-2.0 */
#include "business.h"
#include "tutorial_host.h"
#include <wirelink/udp.h>
int32_t udp_server_on_add(udp_server_node_t *self, const add_request_t *request, add_response_t *response) {
  return peer_add(self->node.user, request, response);
}
int main(int argc, char **argv) {
  static udp_server_node_t server;
  wl_udp_config_t config = {
    .bind_address = "127.0.0.1", .bind_port = 50008,
    .peer_address = "127.0.0.1", .peer_port = 50007
  };
  if (!example_udp_args(argc, argv, &config)) return 1;
  wl_udp_t *udp = NULL;
  if (wl_udp_open(&udp, &config, NULL) != WL_OK) return 1;
  uint32_t instance;
  if (example_instance(&instance) != WL_OK ||
      udp_server_bind_instance(&server, instance, wl_udp_transport(udp)) != WL_OK) {
    wl_udp_close(udp);
    return 1;
  }
  example_install_signals();
  puts("UDP peer server ready"); fflush(stdout);
  uint32_t sequence = 0, due = example_now_ms();
  int status = 0;
  /* [docs:udp_server_loop] */
  while (example_running()) {
    uint32_t now = example_now_ms();
    /* [docs:udp_host_step] */
    if (wl_node_step(&server.node, now) != WL_OK) { status = 1; break; }
    /* [/docs:udp_host_step] */
    if ((int32_t)(now - due) >= 0) {
      telemetry_t sample = {.sample = ++sequence};
      const wl_err_t error = udp_server_send_telemetry(&server, &sample);
      if (error != WL_OK && error != WL_ERR_WOULD_BLOCK) { status = 1; break; }
      due = now + 20;
    }
    now = example_now_ms();
    uint32_t wait = (int32_t)(due - now) > 0 ? due - now : 0;
    if (wl_node_wait(&server.node, now, wait) != WL_OK) { status = 1; break; }
  }
  /* [/docs:udp_server_loop] */
  if (wl_node_close(&server.node, example_now_ms()) != WL_OK) status = 1;
  wl_udp_close(udp);
  return status;
}

/* SPDX-License-Identifier: Apache-2.0 */
#include "device_info.h"
#include "tutorial_host.h"
#include <wirelink/udp.h>
#include <string.h>
/* [docs:device_info_server] */
int32_t info_server_on_get_info(info_server_node_t *self, const info_request_t *request, info_response_t *response) {
  (void)request;
  unsigned *queries = self->node.user;
  response->name.length = 11; memcpy(response->name.data, "workbench\0x", 11);
  response->firmware.length = 6; memcpy(response->firmware.data, "0.0.99", 6);
  response->query_count = ++*queries;
  return 0;
}
/* [/docs:device_info_server] */

int main(int argc, char **argv) {
  static info_server_node_t server;
  wl_udp_config_t config = {
    .bind_address = "127.0.0.1", .bind_port = 50006,
    .peer_address = "127.0.0.1", .peer_port = 50005
  };
  if (!example_udp_args(argc, argv, &config)) return 1;
  wl_udp_t *udp = NULL;
  if (wl_udp_open(&udp, &config, NULL) != WL_OK) return 1;
  if (info_server_bind(&server, wl_udp_transport(udp)) != WL_OK) {
    wl_udp_close(udp);
    return 1;
  }
  example_install_signals();
  unsigned queries = 0;
  server.node.user = &queries;
  puts("Device info server ready");
  fflush(stdout);
  int status = 0;
  const wl_clock_t clock = example_clock();
  while (example_running()) {
    if (wl_node_poll(&server.node, clock, 100U) != WL_OK) {
      status = 1;
      break;
    }
  }
  if (wl_node_close(&server.node, example_now_ms()) != WL_OK) status = 1;
  wl_udp_close(udp);
  return status;
}

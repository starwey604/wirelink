/* SPDX-License-Identifier: Apache-2.0 */
#include "device_info.h"
#include "tutorial_host.h"
#include <string.h>
/* [docs:device_info_server] */
int32_t info_server_on_get_info(info_server_node_t *self, const info_request_t *request, info_response_t *response) {
  (void)request;
  unsigned *queries = self->user;
  response->name.length = 11; memcpy(response->name.data, "workbench\0x", 11);
  response->firmware.length = 6; memcpy(response->firmware.data, "0.0.99", 6);
  response->query_count = ++*queries;
  return 0;
}
/* [/docs:device_info_server] */

int main(int argc, char **argv) {
  static info_server_node_t server;
  example_udp_t *udp = example_udp_create(&server.port);
  if (udp == NULL) return 1;
  if (info_server_init_io(&server, 0U, example_udp_io(udp)) != WL_OK ||
      example_udp_start(udp, INFO_SERVER_TX_WIRE_MAX, argc, argv, 50006, 50005) != WL_OK) {
    example_udp_destroy(udp);
    return 1;
  }
  unsigned queries = 0;
  server.user = &queries;
  puts("Device info server ready");
  fflush(stdout);
  int status = 0;
  while (example_running()) {
    if (example_udp_step(udp, &server.core, 0U) != WL_OK ||
        example_udp_wait(udp, &server.core, 100U) != WL_OK) {
      status = 1;
      break;
    }
  }
  if (example_udp_close(udp, &server.core) != WL_OK) status = 1;
  return status;
}

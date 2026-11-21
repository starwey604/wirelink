/* SPDX-License-Identifier: Apache-2.0 */
#include "calculator.h"
#include "tutorial_host.h"
#include <limits.h>
/* [docs:calculator_server] */
int32_t server_on_add(server_node_t *self, const add_request_t *request, add_response_t *response) {
  (void)self;
  const int64_t sum = (int64_t)request->left + request->right;
  if (sum < INT32_MIN || sum > INT32_MAX) return 1;
  response->sum = (int32_t)sum;
  return 0;
}
/* [/docs:calculator_server] */

int main(int argc, char **argv) {
  static server_node_t server;
  example_udp_t *udp = example_udp_create(&server.port);
  if (udp == NULL) return 1;
  if (server_init_io(&server, 0U, example_udp_io(udp)) != WL_OK ||
      example_udp_start(udp, SERVER_TX_WIRE_MAX, argc, argv, 50004, 50003) != WL_OK) {
    example_udp_destroy(udp);
    return 1;
  }
  puts("Calculator server ready");
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

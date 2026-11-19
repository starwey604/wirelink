/* SPDX-License-Identifier: Apache-2.0 */
#include "probe.h"
#include "wirelink/packet_loopback.h"
#include <stdlib.h>
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    if (!(x))                                                                                      \
      abort();                                                                                     \
  } while (0)
static probe_node_t endpoints[2];
static wl_packet_port_t ports[2];
static uint8_t rx[2][2][256];
static wl_packet_loopback_t pair;
static unsigned completed;
static int32_t execute(void *user, const request_t *request, response_t *response) {
  (void)user;

  response->output = request->input;
  return 0;
}
static void done(void *user, wl_rpc_result_t result, const response_t *response) {
  (void)user;
  CHECK(result.outcome == WL_RPC_SUCCESS && response && response->output == INT32_MIN);
  ++completed;
}
int main(void) {
  CHECK(wl_packet_loopback_init(&pair, &ports[0], &ports[1], NULL, NULL) == WL_OK);
  for (unsigned i = 0; i < 2; ++i) {
    wl_packet_config_t p = {.instance = 1001 + i,
                            .rx_storage = &rx[i][0][0],
                            .rx_unit_size = 256,
                            .rx_storage_size = sizeof(rx[i]),
                            .submit = wl_packet_loopback_submit,
                            .notify = wl_packet_loopback_notify,
                            .user = &pair.directions[i]};
    CHECK(wl_packet_init(&ports[i], &p) == WL_OK);
    probe_handlers_t h = {.on_execute = execute};
    CHECK(probe_init(&endpoints[i], &ports[i], &h, NULL) == WL_OK);
    for (unsigned n = 0; n < 4; ++n) {
      request_t value = {.input = INT32_MIN};
      wl_call_handle_t call;
      CHECK(probe_call_execute_ex(&endpoints[i], &value, 0, 100, done, NULL, &call) == WL_OK);
    }
  }
  for (unsigned i = 0; i < 100 && completed < 8; ++i) {
    CHECK(probe_step(&endpoints[0], 0) == WL_OK &&
          probe_step(&endpoints[1], 0) == WL_OK);
    CHECK(wl_packet_loopback_service(&pair, 2) == WL_OK);
  }
  CHECK(completed == 8);
  for (unsigned i = 0; i < 2; ++i)
    CHECK(wl_endpoint_close(&endpoints[i].core) == WL_OK);
  wl_packet_loopback_stop(&pair);
  for (unsigned i = 0; i < 2; ++i) {
    CHECK(probe_step(&endpoints[i], 0) == WL_OK);
    CHECK(wl_endpoint_quiescent(&endpoints[i].core));
  }
}

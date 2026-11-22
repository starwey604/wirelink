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
  CHECK(wl_packet_loopback_init(&pair, NULL, NULL) == WL_OK);
  for (unsigned i = 0; i < 2; ++i) {
    wl_transport_t *link = wl_packet_loopback_transport(&pair, i);
    probe_handlers_t h = {.on_execute = execute};
    CHECK(probe_bind_instance(&endpoints[i], 1001U + i, link, &h, NULL) == WL_OK);
    for (unsigned n = 0; n < 4; ++n) {
      request_t value = {.input = INT32_MIN};
      wl_call_handle_t call;
      CHECK(probe_call_execute_ex(&endpoints[i], &value, 0, 100, done, NULL, &call) == WL_OK);
    }
  }
  for (unsigned i = 0; i < 100 && completed < 8; ++i) {
    CHECK(wl_node_step(&endpoints[0].node, 0) == WL_OK &&
          wl_node_step(&endpoints[1].node, 0) == WL_OK);
  }
  CHECK(completed == 8);
  for (unsigned side = 0; side < 2U; ++side)
    CHECK(wl_node_close(&endpoints[side].node, 500U) == WL_OK);
}

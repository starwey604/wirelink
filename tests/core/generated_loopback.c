/* SPDX-License-Identifier: Apache-2.0 */
#include "usb.h"
#include "wirelink/packet_loopback.h"
#include <stdlib.h>
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    if (!(x))                                                                                      \
      abort();                                                                                     \
  } while (0)
static usb_node_t endpoints[2];
static wl_packet_loopback_t pair;
static unsigned callbacks, effects;
static int32_t handle(void *user, const setpoint_request_t *request,
                      setpoint_response_t *response) {
  (void)user;
  ++effects;
  *response = (setpoint_response_t){.request_id = request->request_id,
                                          .applied_value = request->value + 1};
  return 0;
}
static void done(void *user, wl_rpc_result_t result, const setpoint_response_t *response) {
  (void)user;
  CHECK(result.outcome == WL_RPC_SUCCESS && response != NULL && response->applied_value == 42);
  ++callbacks;
}
void test_loopback(void) {
  CHECK(wl_packet_loopback_init(&pair, NULL, NULL) == WL_OK);
  for (unsigned i = 0; i < 2; ++i) {
    wl_transport_t *link = wl_packet_loopback_transport(&pair, i);
    usb_handlers_t h = {.on_setpoint = handle};
    CHECK(usb_bind_instance(&endpoints[i], 700U + i, link, &h, NULL) == WL_OK);
    for (unsigned n = 0; n < 4; ++n) {
      setpoint_request_t value = {.request_id = n,
                                        .value = 41};
      wl_call_handle_t call;
      CHECK(usb_call_setpoint_ex(&endpoints[i], &value, 0, 100, done, NULL, &call) == WL_OK);
    }
  }
  for (unsigned n = 0; n < 100 && callbacks < 8; ++n) {
    // Budget 1 forces pending work to survive across owner passes.
    CHECK(wl_endpoint_step(&endpoints[0].node.core, 0, 1) == WL_OK);
    CHECK(wl_endpoint_step(&endpoints[1].node.core, 0, 1) == WL_OK);
    CHECK(wl_packet_loopback_service(&pair, 2) == WL_OK);
    if (callbacks < 8U) {
      int has; uint32_t deadline;
      CHECK(wl_endpoint_ready(&endpoints[0].node.core, 0U, &has, &deadline) ||
            wl_endpoint_ready(&endpoints[1].node.core, 0U, &has, &deadline) ||
            wl_packet_loopback_ready(&pair));
    }
  }
  CHECK(callbacks == 8 && effects == 8);
  CHECK(pair.directions[0].delivered == 8 && pair.directions[1].delivered == 8);
  for (unsigned i = 0; i < 2; ++i)
    CHECK(wl_node_close(&endpoints[i].node, 0) == WL_OK && wl_node_quiescent(&endpoints[i].node));
}

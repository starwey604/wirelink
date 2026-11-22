/* SPDX-License-Identifier: Apache-2.0 */
#include "probe.h"
#include <cstdlib>
static wl_err_t loopback(probe_node_t *node, const uint8_t *data, size_t length) {
  return wl_node_rx_packet(&node->node, data, length);
}
static unsigned cancelled, received;
static void done(void *, wl_rpc_result_t result, const response_t *response) {
  if (result.outcome != WL_RPC_CANCELLED || response)
    std::abort();
  ++cancelled;
}
static void event(void *, const event_t *value) {
  if (value->data.length != 9 || value->data.data[8] != 8)
    std::abort();
  ++received;
}
int main() {
  probe_node_t endpoint{};
  probe_handlers_t handlers{};
  handlers.on_event = event;
  if (probe_bind_sync_instance(&endpoint, 555U, loopback, &handlers, nullptr))
    return 2;
  request_t request{};

  request.input = INT32_MIN;
  wl_call_handle_t handle{};
  if (probe_call_execute_ex(&endpoint, &request, 0, 100, done, nullptr, &handle))
    return 3;
  if (wl_node_cancel(&endpoint.node, handle))
    return 4;
  event_t value{};

  value.data.length = 9;
  for (unsigned i = 0; i < 9; ++i)
    value.data.data[i] = uint8_t(i);
  if (probe_send_event(&endpoint, &value))
    return 5;
  for (unsigned i = 0; i < 5 && (!cancelled || !received); ++i)
    if (wl_node_step(&endpoint.node, 0))
      return 6;
  if (cancelled != 1 || received != 1 || wl_node_close(&endpoint.node, 0) ||
      wl_node_step(&endpoint.node, 0) || !wl_node_quiescent(&endpoint.node))
    return 7;
}

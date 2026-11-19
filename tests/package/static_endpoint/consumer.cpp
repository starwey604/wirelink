/* SPDX-License-Identifier: Apache-2.0 */
#include "probe.h"
#include <cstdlib>
extern "C" wl_packet_port_t *installed_test_port();
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
  auto *port = installed_test_port();
  if (!port)
    return 1;
  probe_node_t endpoint{};
  probe_handlers_t handlers{};
  handlers.on_event = event;
  if (probe_init(&endpoint, port, &handlers, nullptr))
    return 2;
  request_t request{};

  request.input = INT32_MIN;
  wl_call_handle_t handle{};
  if (probe_call_execute_ex(&endpoint, &request, 0, 100, done, nullptr, &handle))
    return 3;
  if (wl_endpoint_cancel(&endpoint.core, handle))
    return 4;
  event_t value{};

  value.data.length = 9;
  for (unsigned i = 0; i < 9; ++i)
    value.data.data[i] = uint8_t(i);
  if (probe_send_event(&endpoint, &value))
    return 5;
  for (unsigned i = 0; i < 5 && (!cancelled || !received); ++i)
    if (probe_step(&endpoint, 0))
      return 6;
  if (cancelled != 1 || received != 1 || wl_endpoint_close(&endpoint.core) ||
      probe_step(&endpoint, 0) || !wl_endpoint_quiescent(&endpoint.core))
    return 7;
}

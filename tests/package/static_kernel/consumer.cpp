/* SPDX-License-Identifier: Apache-2.0 */
#include "probe_kernel.h"
#include <cstdlib>
extern "C" wl_packet_port_t *installed_test_port();
static unsigned cancelled, received;
static void done(void *, wl_kernel_result_t result, const response_t *response) {
  if (result.outcome != WL_KERNEL_CANCELLED || response)
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
  probe_kernel_t endpoint{};
  probe_kernel_handlers_t handlers{};
  handlers.on_message_event = event;
  if (probe_kernel_init(&endpoint, port, &handlers, nullptr, nullptr))
    return 2;
  request_value_t request{};
  request.has_input = true;
  request.input = INT32_MIN;
  wl_kernel_call_handle_t handle{};
  if (probe_kernel_execute_start(&endpoint, &request, 0, 100, done, nullptr, &handle))
    return 3;
  if (wl_kernel_cancel(&endpoint.core, handle))
    return 4;
  event_value_t value{};
  value.has_data = true;
  value.data.length = 9;
  for (unsigned i = 0; i < 9; ++i)
    value.data.data[i] = uint8_t(i);
  if (probe_kernel_send_event(&endpoint, &value))
    return 5;
  for (unsigned i = 0; i < 5 && (!cancelled || !received); ++i)
    if (probe_kernel_step(&endpoint, 0))
      return 6;
  if (cancelled != 1 || received != 1 || wl_kernel_close(&endpoint.core) ||
      probe_kernel_step(&endpoint, 0) || !wl_kernel_quiescent(&endpoint.core))
    return 7;
}

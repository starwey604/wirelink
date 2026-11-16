/* SPDX-License-Identifier: Apache-2.0 */
#include "usb.h"
#include <cstdlib>
#include <type_traits>

extern "C" wl_packet_port_t *cpp_test_port(uint32_t);
static_assert(std::is_standard_layout_v<usb_node_t>);
static_assert(std::is_standard_layout_v<wl_packet_port_t>);
static unsigned completed;
static void done(void *, wl_kernel_result_t result, const setpoint_response_t *reply) {
  if (result.outcome != WL_KERNEL_CANCELLED || reply != nullptr) std::abort();
  ++completed;
}
int main() {
  auto *port = cpp_test_port(7701);
  if (!port || wl_packet_rx_unit_size(port) != 2048) return 1;
  usb_node_t endpoint{};
  usb_handlers_t handlers{};
  if (usb_init(&endpoint, port, &handlers, nullptr)) return 2;
  setpoint_request_t value{};

  value.request_id = UINT32_MAX;
  value.value = UINT32_MAX;
  wl_kernel_call_handle_t handle{};
  if (usb_call_setpoint_ex(&endpoint, &value, 0, 100, done, nullptr, &handle)) return 3;
  if (wl_kernel_cancel(&endpoint.core, handle) || completed) return 4;
  if (usb_step(&endpoint, 0) || completed != 1) return 5;
  if (wl_kernel_close(&endpoint.core) || usb_step(&endpoint, 0) ||
      !wl_kernel_quiescent(&endpoint.core)) return 6;
  // Reuse the same C storage with a new identity after quiescence.
  port = cpp_test_port(7702);
  if (!port || wl_kernel_cancel(&endpoint.core, handle) != WL_ERR_NOT_FOUND) return 7;
  return 0;
}

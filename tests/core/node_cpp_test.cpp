/* SPDX-License-Identifier: Apache-2.0 */
#include "usb.h"
#include <cstdlib>
#include <type_traits>

static wl_err_t send_frame(usb_node_t *, const uint8_t *, size_t) { return WL_OK; }
static_assert(std::is_standard_layout_v<usb_node_t>);
static_assert(offsetof(usb_node_t, node) == 0);
static_assert(std::is_standard_layout_v<wl_packet_port_t>);
static unsigned completed;
static void done(void *, wl_rpc_result_t result, const setpoint_response_t *reply) {
  if (result.outcome != WL_RPC_CANCELLED || reply != nullptr) std::abort();
  ++completed;
}
int main() {
  usb_node_t endpoint{};
  usb_handlers_t handlers{};
  if (usb_bind_sync_instance(&endpoint, 7701, send_frame, &handlers, nullptr)) return 2;
  setpoint_request_t value{};

  value.request_id = UINT32_MAX;
  value.value = UINT32_MAX;
  wl_call_handle_t handle{};
  if (usb_call_setpoint_ex(&endpoint, &value, 0, 100, done, nullptr, &handle)) return 3;
  if (wl_endpoint_cancel(&endpoint.node.core, handle) || completed) return 4;
  if (wl_node_step(&endpoint.node, 0) || completed != 1) return 5;
  if (wl_endpoint_close(&endpoint.node.core) || wl_node_step(&endpoint.node, 0) ||
      !wl_endpoint_quiescent(&endpoint.node.core)) return 6;
  // Reuse the same C storage with a new identity after quiescence.
  if (usb_bind_sync_instance(&endpoint, 7702, send_frame, &handlers, nullptr) ||
      wl_endpoint_cancel(&endpoint.node.core, handle) != WL_ERR_NOT_FOUND) return 7;
  return 0;
}

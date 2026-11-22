/* SPDX-License-Identifier: Apache-2.0 */
#include <wirelink/host/executor.hpp>
#include <future>
int main() {
  wirelink::host::Executor executor;
  wl_node_t node{};
  uint8_t rx[2][64];
  wl_node_layout_t layout{};
  layout.rx_storage = rx[0];
  layout.rx_storage_size = sizeof(rx);
  layout.rx_frame_max = layout.rx_wire_max = sizeof(rx[0]) - 1;
  if (wl_node_bind_sync(&node, &layout, nullptr) != WL_OK ||
      executor.initialize(node) != WL_OK || executor.start() != WL_OK) return 1;
  std::promise<void> ran; auto ready = ran.get_future();
  if (executor.post([&](uint32_t) { ran.set_value(); }) != WL_OK ||
      ready.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
    (void)executor.stop(); return 2;
  }
  if (executor.stop() != WL_OK || executor.last_error() != WL_OK || !wl_node_quiescent(&node)) return 3;
  return 0;
}

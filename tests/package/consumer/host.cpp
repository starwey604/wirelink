/* SPDX-License-Identifier: Apache-2.0 */
#include <wirelink/host/executor.hpp>
#include <future>
extern "C" wl_kernel_t *installed_host_owner(void);
int main() {
  wirelink::host::Executor executor;
  auto *core = installed_host_owner();
  if (!core || executor.initialize(*core) != WL_OK || executor.start() != WL_OK) return 1;
  std::promise<void> ran; auto ready = ran.get_future();
  if (executor.post([&](uint32_t) { ran.set_value(); }) != WL_OK ||
      ready.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
    (void)executor.stop(); return 2;
  }
  if (executor.stop() != WL_OK || executor.last_error() != WL_OK || !wl_kernel_quiescent(core)) return 3;
  return 0;
}

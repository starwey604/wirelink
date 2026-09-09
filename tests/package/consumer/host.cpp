/* SPDX-License-Identifier: Apache-2.0 */
#include <wirelink/host/executor.hpp>
#include <array>

int main() {
  wirelink::host::Executor executor;
  if (executor.state() != wirelink::host::Executor::State::kUninitialized) return 1;
  std::array<uint8_t, 128> payload{}, control{};
  std::array<uint8_t, 256> unit{}, fallback{};
  std::array<uint8_t, 1024> fifo{};
  wl_config_t config{};
  config.max_payload_len = payload.size();
  config.envelope = WL_ENVELOPE_COBS_STREAM;
  config.session_id = 1;
  const wl_storage_t storage{payload.data(), payload.size(), unit.data(), unit.size(),
    control.data(), control.size(), fifo.data(), fifo.size(), fallback.data(), fallback.size()};
  if (executor.initialize(config, storage) != WL_OK || executor.start() != WL_OK) return 2;
  executor.stop();
  const auto activity = executor.activity();
#if defined(WIRELINK_HOST_ACTIVITY)
  // This definition must arrive from the installed target, not consumer flags.
  if (!activity.enabled || activity.get(wirelink::host::ExecutorActivity::notifications) < 2) return 3;
#else
  if (activity.enabled) return 3;
  for (auto count : activity.counts) if (count != 0) return 4;
#endif
  return 0;
}

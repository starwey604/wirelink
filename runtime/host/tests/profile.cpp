/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/diagnostics/host_profile.hpp"
#include <cstdlib>

using namespace wirelink::diagnostics;
int main() {
  reset();
  std::array<std::thread, 4> writers;
  for (auto& writer : writers) writer = std::thread([] {
    for (unsigned i = 0; i < 1000; ++i)
      record(Stage::command_lock, 1, 5000001, 7);
  });
  for (auto& writer : writers) writer.join();
  const auto& value = totals[static_cast<unsigned>(Stage::command_lock)];
  if (value.count != 4000 || value.slow != 4000 || value.max_ns != 5000000 ||
      value.wall_ns != 20000000000ULL || value.cpu_ns != 28000 || slow_count != 4000)
    std::abort();
  for (const auto& sample : slow_samples)
    if (sample.stage != Stage::command_lock || sample.wall_ns != 5000000) std::abort();
  // Only after all writers joined: bounded sample export reports overflow.
  auto* output = std::tmpfile();
  if (!output) return 1;
  dump(output, 0);
  std::fclose(output);
  reset();
  if (value.count != 0 || slow_count != 0) std::abort();
  std::mutex mutex;
  unsigned protected_value = 0;
  for (auto& writer : writers) writer = std::thread([&] {
    for (unsigned i = 0; i < 1000; ++i) {
      MutexScope lock(mutex, Stage::rpc_admit_wait, Stage::rpc_admit_hold);
      ++protected_value;
    }
  });
  for (auto& writer : writers) writer.join();
  if (protected_value != 4000 || totals[static_cast<unsigned>(Stage::rpc_admit_wait)].count != 4000 ||
      totals[static_cast<unsigned>(Stage::rpc_admit_hold)].count != 4000) std::abort();
  reset();
  std::puts("PASS: bounded concurrent profiling, saturation, quiescent reset/export");
}

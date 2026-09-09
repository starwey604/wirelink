/* SPDX-License-Identifier: Apache-2.0 */
#define WL_SPIN_PROFILE_IMPLEMENTATION
#include "spin_profile.hpp"
#include <algorithm>

BUILD_ASSERT(!IS_ENABLED(CONFIG_SMP), "This H7 diagnostic relies on single-core IRQ exclusion");
static SpinSnapshot totals;
static k_spinlock *held;
static uint32_t acquired;

k_spinlock_key_t measured_spin_lock(k_spinlock *lock) {
  const uint32_t begin = timing_counter_get();
  const auto key = k_spin_lock(lock);
  const uint32_t end = timing_counter_get();
  __ASSERT(held == nullptr, "nested profiled locks are not supported");
  held = lock;
  acquired = end;
  const uint32_t wait = end - begin;
  ++totals.calls;
  totals.wait_cycles += wait;
  totals.wait_max = std::max(totals.wait_max, wait);
  return key;
}
void measured_spin_unlock(k_spinlock *lock, k_spinlock_key_t key) {
  const uint32_t end = timing_counter_get();
  __ASSERT(held == lock, "unpaired profiled unlock");
  const uint32_t hold = end - acquired;
  totals.hold_cycles += hold;
  totals.hold_max = std::max(totals.hold_max, hold);
  held = nullptr;
  k_spin_unlock(lock, key);
}
SpinSnapshot spin_snapshot() {
  // Do not use a profiled spinlock to copy the profiling counters.
  const auto key = irq_lock();
  const auto copy = totals;
  irq_unlock(key);
  return copy;
}

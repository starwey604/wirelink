/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/timing/timing.h>
#include <cstdint>

struct SpinSnapshot {
  uint64_t calls{}, wait_cycles{}, hold_cycles{};
  uint32_t wait_max{}, hold_max{};
};
SpinSnapshot spin_snapshot();
k_spinlock_key_t measured_spin_lock(k_spinlock *lock);
void measured_spin_unlock(k_spinlock *lock, k_spinlock_key_t key);
#ifndef WL_SPIN_PROFILE_IMPLEMENTATION
#define k_spin_lock(lock) measured_spin_lock(lock)
#define k_spin_unlock(lock, key) measured_spin_unlock(lock, key)
#endif

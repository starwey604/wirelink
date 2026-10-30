/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_ZEPHYR_TIMEOUT_H
#define WIRELINK_ZEPHYR_TIMEOUT_H

#include <stdint.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bind a caller's relative timeout to the k_uptime_ticks() snapshot taken
 * before querying readiness. Call immediately before waiting. Already-absolute
 * deadlines stay absolute; zero/expired limits do not block, and K_FOREVER
 * stays unbounded. No protocol work or notification acknowledgement occurs.
 * On 32-bit timeout kernels the elapsed interval must be less than one full
 * tick-counter wrap. sampled_ticks is a nonnegative kernel uptime snapshot. */
k_timeout_t wl_zephyr_timeout_since(k_timeout_t maximum, int64_t sampled_ticks);

/* Microsecond allowance with the same snapshot semantics. UINT32_MAX means
 * unbounded and zero means no wait. Finite conversion stays finite even on
 * 32-bit timeout kernels. Conversion runs outside the caller's owner loop. */
k_timeout_t wl_zephyr_timeout_us_since(uint32_t maximum_us, int64_t sampled_ticks);

#ifdef __cplusplus
}
#endif
#endif

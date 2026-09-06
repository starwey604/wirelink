/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_TEST_CLOCK_BRIDGE_H
#define WIRELINK_TEST_CLOCK_BRIDGE_H
#include <stdint.h>
#if defined(_WIN32)
#if defined(CLOCK_BRIDGE_BUILD)
#define CLOCK_EXPORT __declspec(dllexport)
#else
#define CLOCK_EXPORT __declspec(dllimport)
#endif
#else
#define CLOCK_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* Test-only opaque shim, not a published binding API. Native-owned storage and
 * clocks never call Python. close stops use; destroy releases the allocation. */
CLOCK_EXPORT void *clock_bridge_create(uint32_t initial_ms, int native_clock);
CLOCK_EXPORT void clock_bridge_close(void *bridge);
CLOCK_EXPORT void clock_bridge_destroy(void *bridge);
CLOCK_EXPORT int clock_bridge_start(void *bridge, int32_t left, int32_t right,
                                     uint32_t timeout_ms);
CLOCK_EXPORT int clock_bridge_step(void *bridge);
CLOCK_EXPORT int clock_bridge_advance(void *bridge, uint32_t delta_ms);
/* 0 pending, 1 complete, 2 business rejection, 3 timeout, -1 other error. */
CLOCK_EXPORT int clock_bridge_result(void *bridge, int32_t *sum);
CLOCK_EXPORT uint32_t clock_bridge_reads(const void *bridge);
CLOCK_EXPORT uint32_t clock_bridge_handled(const void *bridge);
#ifdef __cplusplus
}
#endif
#endif

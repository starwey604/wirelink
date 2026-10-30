/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_EXECUTOR_TEST_OWNER_H_
#define WIRELINK_EXECUTOR_TEST_OWNER_H_
#include "wirelink/kernel.h"
#ifdef __cplusplus
extern "C" {
#endif
wl_kernel_t *executor_test_core(void);
wl_err_t executor_test_init(uint64_t, wl_packet_sink_fn, wl_packet_notify_fn, void *);
wl_err_t executor_test_call(uint32_t, uint32_t, wl_kernel_complete_fn, void *);
#ifdef __cplusplus
}
#endif
#endif

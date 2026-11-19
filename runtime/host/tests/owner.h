/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_EXECUTOR_TEST_OWNER_H_
#define WIRELINK_EXECUTOR_TEST_OWNER_H_
#include "wirelink/endpoint.h"
#ifdef __cplusplus
extern "C" {
#endif
wl_endpoint_t *executor_test_core(void);
wl_err_t executor_test_init(uint32_t, wl_packet_sink_fn, wl_packet_notify_fn, void *);
wl_err_t executor_test_call(uint32_t, uint32_t, wl_endpoint_complete_fn, void *);
#ifdef __cplusplus
}
#endif
#endif

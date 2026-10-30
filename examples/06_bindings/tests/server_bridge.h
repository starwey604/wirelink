/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <wirelink/host/endpoint.h>
#ifdef __cplusplus
extern "C" {
#endif
size_t calculator_test_server_size(void);
size_t calculator_test_server_alignment(void);
wl_err_t calculator_test_server_prepare(void*, wl_host_endpoint_t*);
wl_err_t calculator_test_server_initialize(void*, uint64_t, wl_packet_sink_fn, wl_packet_notify_fn, void*);
void calculator_test_server_hook(void*, void (*)(void*), void*);
#ifdef __cplusplus
}
#endif

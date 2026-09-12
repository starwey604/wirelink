/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <wirelink/endpoint.h>
#include <wirelink/rpc_result.h>
#ifdef __cplusplus
extern "C" {
#endif
size_t calculator_sdk_endpoint_size(void);
size_t calculator_sdk_endpoint_alignment(void);
wl_err_t calculator_sdk_endpoint_init(void*, wl_endpoint_driver_t*);
void calculator_sdk_endpoint_close(void*);
wl_rpc_completion_t calculator_sdk_add(void*, const void*, void*, uint32_t);
#ifdef __cplusplus
}
#endif

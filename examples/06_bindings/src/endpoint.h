/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <wirelink/endpoint.h>
#include <wirelink/rpc_sync.h>
#ifdef __cplusplus
extern "C" {
#endif
size_t calculator_sdk_endpoint_size(void);
size_t calculator_sdk_endpoint_alignment(void);
wl_err_t calculator_sdk_endpoint_init(void*, wl_endpoint_driver_t*);
void calculator_sdk_endpoint_close(void*);
wl_err_t calculator_sdk_cancel(void*, const wl_rpc_call_t*);
typedef struct {
  void* endpoint;
  const void* request;
  void* response;
  wl_rpc_sync_notify_fn notify;
  void* notify_context;
} calculator_sdk_call_t;
wl_rpc_completion_t calculator_sdk_add(void*, const void*, void*, uint32_t);
wl_err_t calculator_sdk_add_submit(calculator_sdk_call_t*, wl_time_ms_t, wl_rpc_sync_notify_fn, void*, wl_rpc_call_t*);
#ifdef __cplusplus
}
#endif

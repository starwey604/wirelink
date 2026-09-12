/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <wirelink/endpoint.h>
#include <wirelink/rpc_sync.h>
#ifdef __cplusplus
extern "C" {
#endif
size_t device_sdk_endpoint_size(void);
size_t device_sdk_endpoint_alignment(void);
wl_err_t device_sdk_endpoint_init(void*, wl_endpoint_driver_t*);
void device_sdk_endpoint_close(void*);
wl_err_t device_sdk_cancel(void*, const wl_rpc_call_t*);
typedef struct {
  void* endpoint;
  const void* request;
  void* response;
  wl_rpc_sync_notify_fn notify;
  void* notify_context;
} device_sdk_call_t;
wl_rpc_completion_t device_sdk_configure(void*, const void*, void*, uint32_t);
wl_rpc_completion_t device_sdk_get_info(void*, const void*, void*, uint32_t);
wl_err_t device_sdk_configure_submit(device_sdk_call_t*, wl_time_ms_t, wl_rpc_sync_notify_fn, void*, wl_rpc_call_t*);
wl_err_t device_sdk_get_info_submit(device_sdk_call_t*, wl_time_ms_t, wl_rpc_sync_notify_fn, void*, wl_rpc_call_t*);
#ifdef __cplusplus
}
#endif

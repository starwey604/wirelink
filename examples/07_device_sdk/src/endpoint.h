/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <wirelink/host/endpoint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  void* endpoint;
  const void* request;
  void* response;
  wl_host_rpc_notify_fn notify;
  void* notify_context;
} device_sdk_call_t;
wl_err_t device_sdk_configure_submit(device_sdk_call_t*, uint32_t, uint32_t, wl_host_rpc_notify_fn, void*, wl_call_handle_t*);
wl_err_t device_sdk_get_info_submit(device_sdk_call_t*, uint32_t, uint32_t, wl_host_rpc_notify_fn, void*, wl_call_handle_t*);
#ifdef __cplusplus
}
#endif

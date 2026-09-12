/* SPDX-License-Identifier: Apache-2.0 */
// Endpoint callbacks are compiled in C with exactly the runtime's C types.
#include "endpoint.h"
#include "device_endpoint.h"
#include <wirelink/platform.h>
#include <string.h>
size_t device_sdk_endpoint_size(void) { return sizeof(device_endpoint_t); }
size_t device_sdk_endpoint_alignment(void) { return _Alignof(device_endpoint_t); }
wl_err_t device_sdk_endpoint_init(void* storage, wl_endpoint_driver_t* driver) {
  device_endpoint_t* endpoint = storage;
  memset(endpoint, 0, sizeof(*endpoint));
  const wl_err_t status = device_endpoint_init(endpoint, wl_platform_environment());
  if (status == WL_OK) *driver = device_endpoint_driver(endpoint);
  return status;
}
void device_sdk_endpoint_close(void* storage) { (void)device_endpoint_close(storage); }
wl_rpc_completion_t device_sdk_configure(void* endpoint, const void* request, void* response, uint32_t timeout_ms) {
  return device_endpoint_configure_sync(endpoint, request, response, timeout_ms);
}
wl_rpc_completion_t device_sdk_get_info(void* endpoint, const void* request, void* response, uint32_t timeout_ms) {
  return device_endpoint_get_info_sync(endpoint, request, response, timeout_ms);
}

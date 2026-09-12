/* SPDX-License-Identifier: Apache-2.0 */
// Endpoint callbacks are compiled in C with exactly the runtime's C types.
#include "endpoint.h"
#include "calculator_endpoint.h"
#include <wirelink/platform.h>
#include <string.h>
size_t calculator_sdk_endpoint_size(void) { return sizeof(calculator_endpoint_t); }
size_t calculator_sdk_endpoint_alignment(void) { return _Alignof(calculator_endpoint_t); }
wl_err_t calculator_sdk_endpoint_init(void* storage, wl_endpoint_driver_t* driver) {
  calculator_endpoint_t* endpoint = storage;
  memset(endpoint, 0, sizeof(*endpoint));
  const wl_err_t status = calculator_endpoint_init(endpoint, wl_platform_environment());
  if (status == WL_OK) *driver = calculator_endpoint_driver(endpoint);
  return status;
}
void calculator_sdk_endpoint_close(void* storage) { (void)calculator_endpoint_close(storage); }
wl_rpc_completion_t calculator_sdk_add(void* endpoint, const void* request, void* response, uint32_t timeout_ms) {
  return calculator_endpoint_add_sync(endpoint, request, response, timeout_ms);
}

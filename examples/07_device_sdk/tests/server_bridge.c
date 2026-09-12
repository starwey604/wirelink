/* SPDX-License-Identifier: Apache-2.0 */
#include "server_bridge.h"
#include "service.h"
#include "device_endpoint.h"
#include <wirelink/platform.h>
#include <string.h>

typedef struct {
  device_endpoint_t endpoint;
  device_test_state_t state;
} device_test_server_t;
size_t device_test_server_size(void) { return sizeof(device_test_server_t); }
size_t device_test_server_alignment(void) { return _Alignof(device_test_server_t); }
wl_err_t device_test_server_init(void* storage, wl_endpoint_driver_t* driver) {
  device_test_server_t* server = storage;
  device_endpoint_config_t config;
  memset(server, 0, sizeof(*server));
  wl_err_t status = device_endpoint_config_defaults(&config, wl_platform_environment());
  if (status != WL_OK) return status;
  config.on_get_info = device_test_get_info;
  config.on_configure = device_test_configure;
  config.user_data = &server->state;
  status = device_endpoint_init_config(&server->endpoint, &config);
  if (status == WL_OK) *driver = device_endpoint_driver(&server->endpoint);
  return status;
}

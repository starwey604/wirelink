/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EXAMPLE_DEVICE_SERVICE_H
#define EXAMPLE_DEVICE_SERVICE_H
#include "device_server_endpoint.h"

/* Application state, not endpoint storage or RPC bookkeeping. */
typedef struct {
  char name[32];
  size_t name_length;
  int32_t registers[4];
  uint32_t writes;
  int32_t lower, upper;
} device_service_t;

enum { DEVICE_INVALID_ARGUMENT = 1, DEVICE_OUT_OF_RANGE = 2, DEVICE_BUSY = 3 };

void device_service_init(device_service_t *device);
void device_service_bind(device_server_endpoint_config_t *config);
void calculator_service_bind(device_server_endpoint_config_t *config);
#endif

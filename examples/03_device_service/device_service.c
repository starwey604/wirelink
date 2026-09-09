/* SPDX-License-Identifier: Apache-2.0 */
#include "device_service.h"
#include <string.h>

void device_service_init(device_service_t *device) {
  memset(device, 0, sizeof(*device));
  memcpy(device->name, "workbench", 9);
  device->name_length = 9;
  device->lower = -100;
  device->upper = 100;
}

static int32_t ping(void *context, const ping_request_value_t *request,
                    ping_response_value_t *response) {
  (void)context;
  response->has_nonce = true;
  response->nonce = request->nonce;
  return 0;
}

static int32_t get_info(void *context, const get_info_request_value_t *request,
                        get_info_response_value_t *response) {
  const device_service_t *device = context;
  (void)request;
  response->has_name = true;
  response->name.length = device->name_length;
  memcpy(response->name.data, device->name, device->name_length);
  return 0;
}

static int32_t set_name(void *context, const set_name_request_value_t *request,
                        set_name_response_value_t *response) {
  device_service_t *device = context;
  (void)response;
  if (request->name.length == 0) return DEVICE_INVALID_ARGUMENT;
  memcpy(device->name, request->name.data, request->name.length);
  device->name[request->name.length] = '\0';
  device->name_length = request->name.length;
  return 0;
}

static int32_t read_register(void *context, const read_register_request_value_t *request,
                             read_register_response_value_t *response) {
  const device_service_t *device = context;
  if (request->index >= 4) return DEVICE_INVALID_ARGUMENT;
  response->has_value = true;
  response->value = device->registers[request->index];
  return 0;
}

static int32_t write_register(void *context, const write_register_request_value_t *request,
                              write_register_response_value_t *response) {
  device_service_t *device = context;
  (void)response;
  if (request->index >= 4) return DEVICE_INVALID_ARGUMENT;
  if (request->value < device->lower || request->value > device->upper)
    return DEVICE_OUT_OF_RANGE;
  device->registers[request->index] = request->value;
  ++device->writes;
  return 0;
}

static int32_t get_counters(void *context, const get_counters_request_value_t *request,
                            get_counters_response_value_t *response) {
  const device_service_t *device = context;
  (void)request;
  response->has_writes = true;
  response->writes = device->writes;
  return 0;
}

static int32_t reset_counters(void *context, const reset_counters_request_value_t *request,
                              reset_counters_response_value_t *response) {
  device_service_t *device = context;
  (void)request;
  (void)response;
  device->writes = 0;
  return 0;
}

/* extension: handlers */

void device_service_bind(device_server_endpoint_config_t *config) {
  config->on_ping = ping;
  config->on_get_info = get_info;
  config->on_set_name = set_name;
  config->on_read_register = read_register;
  config->on_write_register = write_register;
  config->on_get_counters = get_counters;
  config->on_reset_counters = reset_counters;
  /* extension: registration */
}

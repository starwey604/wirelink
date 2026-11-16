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

int32_t device_server_on_ping(device_server_node_t *self, const ping_request_t *request,
                    ping_response_t *response) {
  (void)self;
  response->nonce = request->nonce;
  return 0;
}

int32_t device_server_on_get_info(device_server_node_t *self, const get_info_request_t *request,
                        get_info_response_t *response) {
  const device_service_t *device = self->user;
  (void)request;
  response->name.length = device->name_length;
  memcpy(response->name.data, device->name, device->name_length);
  return 0;
}

int32_t device_server_on_set_name(device_server_node_t *self, const set_name_request_t *request,
                        set_name_response_t *response) {
  device_service_t *device = self->user;
  (void)response;
  if (request->name.length == 0) return DEVICE_INVALID_ARGUMENT;
  memcpy(device->name, request->name.data, request->name.length);
  device->name[request->name.length] = '\0';
  device->name_length = request->name.length;
  return 0;
}

int32_t device_server_on_read_register(device_server_node_t *self, const read_register_request_t *request,
                             read_register_response_t *response) {
  const device_service_t *device = self->user;
  if (request->index >= 4) return DEVICE_INVALID_ARGUMENT;
  response->value = device->registers[request->index];
  return 0;
}

int32_t device_server_on_write_register(device_server_node_t *self, const write_register_request_t *request,
                              write_register_response_t *response) {
  device_service_t *device = self->user;
  (void)response;
  if (request->index >= 4) return DEVICE_INVALID_ARGUMENT;
  if (request->value < device->lower || request->value > device->upper)
    return DEVICE_OUT_OF_RANGE;
  device->registers[request->index] = request->value;
  ++device->writes;
  return 0;
}

int32_t device_server_on_get_counters(device_server_node_t *self, const get_counters_request_t *request,
                            get_counters_response_t *response) {
  const device_service_t *device = self->user;
  (void)request;
  response->writes = device->writes;
  return 0;
}

int32_t device_server_on_reset_counters(device_server_node_t *self, const reset_counters_request_t *request,
                              reset_counters_response_t *response) {
  device_service_t *device = self->user;
  (void)request;
  (void)response;
  device->writes = 0;
  return 0;
}

/* extension: handlers */

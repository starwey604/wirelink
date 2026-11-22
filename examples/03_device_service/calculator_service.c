/* SPDX-License-Identifier: Apache-2.0 */
#include "device_service.h"
#include <limits.h>

int32_t device_server_on_add(device_server_node_t *self, const add_request_t *request,
                   add_response_t *response) {
  const int64_t sum = (int64_t)request->left + request->right;
  (void)self;
  if (sum < INT32_MIN || sum > INT32_MAX) return DEVICE_OUT_OF_RANGE;
  response->sum = (int32_t)sum;
  return 0;
}

int32_t device_server_on_scale(device_server_node_t *self, const scale_request_t *request,
                     scale_response_t *response) {
  const int64_t value = (int64_t)request->value * request->factor;
  (void)self;
  if (value < INT32_MIN || value > INT32_MAX) return DEVICE_OUT_OF_RANGE;
  response->value = (int32_t)value;
  return 0;
}

int32_t device_server_on_get_limits(device_server_node_t *self, const get_limits_request_t *request,
                          get_limits_response_t *response) {
  const device_service_t *device = self->node.user;
  (void)request;
  response->lower = device->lower;
  response->upper = device->upper;
  return 0;
}

int32_t device_server_on_set_limits(device_server_node_t *self, const set_limits_request_t *request,
                          set_limits_response_t *response) {
  device_service_t *device = self->node.user;
  (void)response;
  if (request->lower > request->upper) return DEVICE_INVALID_ARGUMENT;
  device->lower = request->lower;
  device->upper = request->upper;
  return 0;
}

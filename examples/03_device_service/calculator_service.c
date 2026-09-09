/* SPDX-License-Identifier: Apache-2.0 */
#include "device_service.h"
#include <limits.h>

static int32_t add(void *context, const add_request_value_t *request,
                   add_response_value_t *response) {
  const int64_t sum = (int64_t)request->left + request->right;
  (void)context;
  if (sum < INT32_MIN || sum > INT32_MAX) return DEVICE_OUT_OF_RANGE;
  response->has_sum = true;
  response->sum = (int32_t)sum;
  return 0;
}

static int32_t scale(void *context, const scale_request_value_t *request,
                     scale_response_value_t *response) {
  const int64_t value = (int64_t)request->value * request->factor;
  (void)context;
  if (value < INT32_MIN || value > INT32_MAX) return DEVICE_OUT_OF_RANGE;
  response->has_value = true;
  response->value = (int32_t)value;
  return 0;
}

static int32_t get_limits(void *context, const get_limits_request_value_t *request,
                          get_limits_response_value_t *response) {
  const device_service_t *device = context;
  (void)request;
  response->has_lower = response->has_upper = true;
  response->lower = device->lower;
  response->upper = device->upper;
  return 0;
}

static int32_t set_limits(void *context, const set_limits_request_value_t *request,
                          set_limits_response_value_t *response) {
  device_service_t *device = context;
  (void)response;
  if (request->lower > request->upper) return DEVICE_INVALID_ARGUMENT;
  device->lower = request->lower;
  device->upper = request->upper;
  return 0;
}

void calculator_service_bind(device_server_endpoint_config_t *config) {
  config->on_add = add;
  config->on_scale = scale;
  config->on_get_limits = get_limits;
  config->on_set_limits = set_limits;
}

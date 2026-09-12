/* SPDX-License-Identifier: Apache-2.0 */
#include "service.h"
#include <string.h>

int32_t device_test_get_info(void* context, const info_request_value_t* request,
                            info_response_value_t* response) {
  (void)context;
  (void)request;
  info_response_value_clear(response);
  response->has_name = true;
  response->name.length = 8;
  memcpy(response->name.data, "device\0x", 8);
  response->has_settings = true;
  response->settings.has_enabled = true;
  response->settings.enabled = true;
  response->settings.has_token = true;
  response->settings.token.length = 3;
  memcpy(response->settings.token.data, "\0\x80\xff", 3);
  response->settings.has_gains = true;
  response->settings.gains[0] = 1.0F;
  response->settings.gains[1] = -2.5F;
  response->settings.gains[2] = 3.25F;
  response->settings.has_mode = true;
  response->settings.mode = 12345; /* Future enum value, unknown to the SDK. */
  return 0;
}

int32_t device_test_configure(void* context, const configure_request_value_t* request,
                             configure_response_value_t* response) {
  device_test_state_t* state = (device_test_state_t*)context;
  if (request->settings.token.length != 0 && request->settings.token.data[0] == 255)
    return 7;
  configure_response_value_clear(response);
  response->has_settings = true;
  response->settings = request->settings;
  response->has_opaque = request->has_opaque;
  response->opaque.length = request->opaque.length;
  memcpy(response->opaque.data, request->opaque.data, request->opaque.length);
  response->has_backup = request->has_backup;
  response->backup = request->backup;
  response->has_count = true;
  response->count = ++state->calls;
  return 0;
}

/* SPDX-License-Identifier: Apache-2.0 */
#include "service.h"
#include <string.h>

int32_t device_test_get_info(void* context, const peer_info_request_t* request,
                            peer_info_response_t* response) {
  (void)context;
  (void)request;
  peer_info_response_clear(response);

  response->name.length = 8;
  memcpy(response->name.data, "device\0x", 8);

  response->settings.enabled = true;

  response->settings.token.length = 3;
  memcpy(response->settings.token.data, "\0\x80\xff", 3);

  response->settings.gains[0] = 1.0F;
  response->settings.gains[1] = -2.5F;
  response->settings.gains[2] = 3.25F;
  response->settings.has_mode = true;
  response->settings.mode = 12345; /* Future enum value, unknown to the SDK. */
  return 0;
}

int32_t device_test_configure(void* context, const peer_configure_request_t* request,
                             peer_configure_response_t* response) {
  device_test_state_t* state = (device_test_state_t*)context;
  if (request->settings.token.length != 0 && request->settings.token.data[0] == 255)
    return 7;
  peer_configure_response_clear(response);

  response->settings = request->settings;
  response->has_opaque = request->has_opaque;
  response->opaque.length = request->opaque.length;
  memcpy(response->opaque.data, request->opaque.data, request->opaque.length);
  response->has_backup = request->has_backup;
  response->backup = request->backup;

  response->count = ++state->calls;
  return 0;
}

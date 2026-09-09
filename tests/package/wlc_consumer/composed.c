/* SPDX-License-Identifier: Apache-2.0 */
#include "device_server_endpoint.h"
#include "../../support/test_environment.h"

_Static_assert(DEVICE_SERVER_ENDPOINT_MAX_PAYLOAD == DEVICE_TELEMETRY_MAX_ENCODED_SIZE,
               "outbound telemetry must contribute to endpoint sizing");

static wl_time_ms_t now(void *context) { (void)context; return 1U; }
static int32_t ping(void *context, const ping_request_value_t *request,
                    ping_response_value_t *response) {
  (void)context;
  response->has_nonce = true;
  response->nonce = request->nonce;
  return 0;
}

int main(void) {
  static device_server_endpoint_t endpoint;
  device_server_endpoint_config_t config;
  unsigned business_state = 0;
  const wl_clock_t clock = {now, NULL};
  if (device_server_endpoint_config_defaults(&config, test_environment_id(1, clock)) != WL_OK) return 1;
  config.user_data = &business_state;
  config.on_ping = ping;
  if (device_server_endpoint_init_config(&endpoint, &config) != WL_OK) return 2;
  return device_server_endpoint_close(&endpoint) == WL_OK ? 0 : 3;
}

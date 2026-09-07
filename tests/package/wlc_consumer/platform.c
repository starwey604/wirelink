/* SPDX-License-Identifier: Apache-2.0 */
#include "calculator_endpoint.h"
#include "wirelink/platform.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int main(void) {
  static calculator_endpoint_t first, second;
  calculator_endpoint_config_t config;
  uint64_t first_id, second_id;
  CHECK(calculator_endpoint_config_defaults(&config, wl_platform_environment()) == WL_OK);
  CHECK(config.link.session_id == 0U);
  CHECK(calculator_endpoint_init_config(&first, &config) == WL_OK);
  CHECK(calculator_endpoint_init_config(&second, &config) == WL_OK);
  first_id = wl_link_session_id(wl_endpoint_link(calculator_endpoint_handle(&first)));
  second_id = wl_link_session_id(wl_endpoint_link(calculator_endpoint_handle(&second)));
  CHECK(first_id != 0U && second_id != 0U && first_id != second_id);
  CHECK(config.link.session_id == 0U);
  CHECK(calculator_endpoint_close(&first) == WL_OK);
  CHECK(calculator_endpoint_init_config(&first, &config) == WL_OK);
  CHECK(wl_link_session_id(wl_endpoint_link(calculator_endpoint_handle(&first))) != first_id);
  CHECK(calculator_endpoint_close(&first) == WL_OK);
  CHECK(calculator_endpoint_close(&second) == WL_OK);
  return 0;
}

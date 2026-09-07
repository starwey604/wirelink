/* SPDX-License-Identifier: Apache-2.0 */
#include "calculator_endpoint.h"
#include "../../support/test_environment.h"
#include "wirelink/storage/fixed_pool.h"
static wl_time_ms_t now(void *context) { (void)context; return 0; }
int main(void) {
  static union { calculator_endpoint_t alignment; unsigned char bytes[sizeof(calculator_endpoint_t)]; } memory;
  wl_fixed_pool_t pool;
  calculator_endpoint_t *endpoint = NULL;
  calculator_endpoint_config_t config;
  if (wl_fixed_pool_init(&pool, memory.bytes, sizeof(memory.bytes), sizeof(calculator_endpoint_t),
      CALCULATOR_ENDPOINT_ALIGNMENT, 1) != WL_OK) return 1;
  wl_allocator_t allocator = wl_fixed_pool_allocator(&pool);
  if (calculator_endpoint_config_defaults(&config, test_environment_id(1, (wl_clock_t){0})) != WL_OK) return 2;
  config.environment.clock = (wl_clock_t){now, NULL};
  if (calculator_endpoint_create(&endpoint, &config, &allocator) != WL_OK) return 3;
  if (wl_fixed_pool_in_use(&pool) != 1 || calculator_endpoint_step(endpoint) != WL_OK) return 4;
  if (calculator_endpoint_destroy(&endpoint) != WL_OK) return 5;
  return endpoint == NULL && wl_fixed_pool_in_use(&pool) == 0 ? 0 : 6;
}

/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/platform.h"
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

static wl_time_ms_t native_now(void *context) {
  (void)context;
  return k_uptime_get_32();
}
static wl_err_t native_session(void *context, uint64_t *out) {
  (void)context;
  if (out == NULL) return WL_ERR_INVALID_ARG;
#if defined(CONFIG_CSPRNG_ENABLED) && !defined(CONFIG_TEST_CSPRNG_GENERATOR)
  for (unsigned i = 0U; i < 4U; ++i) {
    uint64_t value;
    if (sys_csrand_get(&value, sizeof(value)) != 0) return WL_ERR_IO;
    if (value != 0U) { *out = value; return WL_OK; }
  }
  return WL_ERR_IO;
#else
  /* Simulators/bare boards must explicitly inject their deterministic source. */
  return WL_ERR_NOT_SUPPORTED;
#endif
}
wl_environment_t wl_platform_environment(void) {
  wl_environment_t environment = {{native_now, NULL}, {native_session, NULL}};
  return environment;
}

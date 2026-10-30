/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/platform.h"
#include <stdio.h>
int session_run(wl_session_source_t source);
int main(void) {
  wl_environment_t native = wl_platform_environment();
  uint64_t previous = 0, next = 0;
  puts("SESSION start");
  for (unsigned i = 0; i < 128; ++i) {
    wl_err_t error = wl_session_next(native.session, previous, &next);
#if defined(__ZEPHYR__) && !defined(SESSION_REQUIRE_NATIVE) && \
    (!defined(CONFIG_CSPRNG_ENABLED) || defined(CONFIG_TEST_CSPRNG_GENERATOR))
    if (error == WL_ERR_NOT_SUPPORTED) {
      puts("SESSION platform RNG unavailable (sim); injected source exercised below");
      break;
    }
#endif
    if (error != WL_OK || next == 0 || next == previous) {
      printf("SESSION platform failure=%d iteration=%u\n", (int)error, i); return 1;
    }
    previous = next;
    if (i == 127) puts("SESSION platform RNG 128 PASS");
  }
  if (session_run(native.session)) return 1;
  puts("SESSION ALL PASS"); return 0;
}

/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/platform.h"
#include "rr_endpoint.h"
#include <stdio.h>
#ifdef __ZEPHYR__
#include <zephyr/kernel.h>
#endif
int rr_run(void);
int ru_run(void);
int ur_run(void);
int uu_run(void);

int main(void) {
  wl_environment_t native = wl_platform_environment();
  uint64_t previous = 0U, next = 0U;
  wl_err_t error;
  puts("SESSION_P0 ABI=26 start");
  for (unsigned i = 0; i < 128U; ++i) {
    error = wl_session_next(native.session, previous, &next);
#if defined(__ZEPHYR__) && !defined(SESSION_REQUIRE_NATIVE) && \
    (!defined(CONFIG_CSPRNG_ENABLED) || defined(CONFIG_TEST_CSPRNG_GENERATOR))
    if (error == WL_ERR_NOT_SUPPORTED) {
      puts("SESSION_P0 platform RNG unavailable (sim); injected source exercised below");
      break;
    }
#endif
    if (error != WL_OK || next == previous || next == 0U) {
      printf("SESSION_P0 platform failure=%d iteration=%u\n", (int)error, i);
      return 1;
    }
    previous = next;
    if (i == 127U) puts("SESSION_P0 platform RNG 128 PASS");
  }
  if (error == WL_OK) {
    static rr_endpoint_t endpoint;
    uint64_t initial;
    if (rr_endpoint_init(&endpoint, native) != WL_OK) return 2;
    initial = wl_link_session_id(wl_endpoint_link(rr_endpoint_handle(&endpoint)));
    if (initial == 0U || rr_endpoint_close(&endpoint) != WL_OK) return 2;
    if (rr_endpoint_init(&endpoint, native) != WL_OK) return 2;
    if (wl_link_session_id(wl_endpoint_link(rr_endpoint_handle(&endpoint))) == initial ||
        rr_endpoint_close(&endpoint) != WL_OK) return 2;
    puts("SESSION_P0 platform endpoint reinit PASS");
  }
  if (rr_run() || ru_run() || ur_run() || uu_run()) return 1;
  puts("SESSION_P0 ALL PASS");
  return 0;
}

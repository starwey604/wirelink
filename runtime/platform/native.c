/* SPDX-License-Identifier: Apache-2.0 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "wirelink/platform.h"
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <Security/SecRandom.h>
#include <time.h>
#elif defined(__linux__)
#include <errno.h>
#include <sys/random.h>
#include <time.h>
#else
#error "No native Wirelink environment for this platform; inject wl_environment_t"
#endif

static wl_time_ms_t native_now(void *context) {
  (void)context;
#if defined(_WIN32)
  return (wl_time_ms_t)GetTickCount64();
#else
  struct timespec now;
  /* CLOCK_MONOTONIC is mandatory for this backend. */
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0U;
  return (wl_time_ms_t)((uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U);
#endif
}

static wl_err_t native_session(void *context, uint64_t *out) {
  (void)context;
  if (out == NULL) return WL_ERR_INVALID_ARG;
  /* Bound retries for the reserved zero value; do not loop on provider failure. */
  for (unsigned i = 0U; i < 4U; ++i) {
    uint64_t value;
#if defined(_WIN32)
    if (BCryptGenRandom(NULL, (PUCHAR)&value, sizeof(value),
                       BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return WL_ERR_IO;
#elif defined(__APPLE__)
    if (SecRandomCopyBytes(kSecRandomDefault, sizeof(value), (uint8_t *)&value) != errSecSuccess)
      return WL_ERR_IO;
#else
    ssize_t count = getrandom(&value, sizeof(value), GRND_NONBLOCK);
    if (count < 0 && (errno == EAGAIN || errno == EINTR)) return WL_ERR_WOULD_BLOCK;
    if (count != (ssize_t)sizeof(value)) return WL_ERR_IO;
#endif
    if (value != 0U) { *out = value; return WL_OK; }
  }
  return WL_ERR_IO;
}

wl_environment_t wl_platform_environment(void) {
  wl_environment_t environment = {{native_now, NULL}, {native_session, NULL}};
  return environment;
}

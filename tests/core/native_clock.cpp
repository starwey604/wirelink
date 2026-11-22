/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/host/clock.hpp"
extern "C" wl_clock_t clock_bridge_native_clock(void) {
  return wirelink::host::monotonic_clock();
}

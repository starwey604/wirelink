/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_HOST_CLOCK_HPP
#define WIRELINK_HOST_CLOCK_HPP

#include "wirelink/clock.h"
#include <chrono>

namespace wirelink::host {
inline wl_time_ms_t monotonic_now_ms(void*) noexcept {
    using namespace std::chrono;
    return static_cast<wl_time_ms_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
inline wl_clock_t monotonic_clock() noexcept {
    return {monotonic_now_ms, nullptr};
}
} // namespace wirelink::host
#endif

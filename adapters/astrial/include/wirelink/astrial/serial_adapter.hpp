/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIRELINK_ASTRIAL_SERIAL_ADAPTER_HPP_
#define WIRELINK_ASTRIAL_SERIAL_ADAPTER_HPP_

#include <astrial/Serial.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>

#include <tl/expected.hpp>

#include "wirelink/wirelink.h"
#include "wirelink/adapter.h"

namespace wirelink::astrial
{
struct SerialConfig
{
    using ActivityCallback = void (*)(void* user_data) noexcept;

    std::string port;
    uint32_t baud_rate{115200};
    Parity parity{Parity::None};
    StopBits stop_bits{StopBits::One};
    DataBits data_bits{DataBits::Eight};
    bool auto_reconnect{true};
    std::chrono::milliseconds reconnect_interval{std::chrono::seconds(2)};
    // Runs on Astrial's I/O thread after RX/TX completion state is published.
    ActivityCallback activity_callback{};
    void* activity_user_data{};
};

struct SerialAdapterStats
{
    uint64_t rx_reservations{};
    uint64_t rx_bytes{};
    uint64_t rx_pauses{};
    uint64_t tx_submissions{};
    uint64_t tx_completions{};
    uint64_t errors{};
    bool started{};
    bool rx_paused{};
    bool tx_active{};
};

class SerialAdapter
{
public:
    static tl::expected<std::unique_ptr<SerialAdapter>, std::error_code>
    open(wl_ctx_t& link, const SerialConfig& config);

    ~SerialAdapter();

    SerialAdapter(const SerialAdapter&) = delete;
    SerialAdapter& operator=(const SerialAdapter&) = delete;
    SerialAdapter(SerialAdapter&&) = delete;
    SerialAdapter& operator=(SerialAdapter&&) = delete;

    // Call from Wirelink's single consumer context after wl_poll(). It
    // forwards deferred TX completion and re-arms RX after ring backpressure.
    int service();
    void quiesce() noexcept;
    [[nodiscard]] std::uint32_t deadline_hint(wl_time_ms_t now_ms) const noexcept;
    void get_stats(SerialAdapterStats& out_stats) const;
    void get_common_stats(wl_adapter_stats_t& out_stats) const noexcept;
    Serial& serial();

private:
    class Impl;

    SerialAdapter(wl_ctx_t& link, Serial&& serial,
                  SerialConfig::ActivityCallback activity_callback,
                  void* activity_user_data);
    int start();
    static wl_sink_result_t sink(void* user_data, wl_io_token_t token,
                                 const uint8_t* data, size_t length);

    std::unique_ptr<Impl> m_impl;
};
}

#endif // WIRELINK_ASTRIAL_SERIAL_ADAPTER_HPP_

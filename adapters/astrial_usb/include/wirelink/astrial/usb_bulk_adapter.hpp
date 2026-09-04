/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIRELINK_ASTRIAL_USB_BULK_ADAPTER_HPP_
#define WIRELINK_ASTRIAL_USB_BULK_ADAPTER_HPP_

#include <astrial/Usb.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <system_error>

#include <tl/expected.hpp>

#include "wirelink/port.h"
#include "wirelink/adapter.h"

namespace wirelink::astrial
{
enum class UsbBulkWakePolicy
{
    AllCompletions,
    ReceiveOnly,
};

struct UsbBulkAdapterConfig
{
    using ActivityCallback = void (*)(void* user_data) noexcept;

    UsbBulkConfig usb;
    std::size_t maximum_read_size{512};
    std::uint8_t unit_queue_slots{4};
    UsbBulkWakePolicy wake_policy{UsbBulkWakePolicy::AllCompletions};
    // Optional non-blocking notification used to wake an external Wirelink
    // owner. It runs on Astrial's USB event thread after RX/TX activity has
    // been published; protocol work remains on the owner's thread.
    ActivityCallback activity_callback{};
    void* activity_user_data{};
};

struct UsbBulkAdapterStats
{
    uint64_t rx_claims{};
    uint64_t rx_bytes{};
    uint64_t rx_pauses{};
    uint64_t tx_submissions{};
    uint64_t tx_completions{};
    uint64_t activity_notifications{};
    uint64_t wait_calls{};
    uint64_t wait_wakeups{};
    uint64_t wait_timeouts{};
    uint64_t errors{};
    bool started{};
    bool rx_paused{};
    bool tx_active{};
};

class UsbBulkAdapter
{
public:
    static tl::expected<std::unique_ptr<UsbBulkAdapter>, std::error_code>
    open(wl_ctx_t& link, const UsbBulkAdapterConfig& config);

    ~UsbBulkAdapter();

    UsbBulkAdapter(const UsbBulkAdapter&) = delete;
    UsbBulkAdapter& operator=(const UsbBulkAdapter&) = delete;
    UsbBulkAdapter(UsbBulkAdapter&&) = delete;
    UsbBulkAdapter& operator=(UsbBulkAdapter&&) = delete;

    // Call from Wirelink's single-consumer context after wl_poll().
    int service();
    void quiesce() noexcept;
    [[nodiscard]] std::uint32_t deadline_hint(wl_time_ms_t now_ms) const noexcept;
    // Blocks the single-consumer context until RX/TX activity or timeout.
    // Pending notifications are coalesced before returning.
    bool wait_for_activity(std::chrono::nanoseconds timeout);
    void get_stats(UsbBulkAdapterStats& out_stats) const;
    void get_common_stats(wl_adapter_stats_t& out_stats) const noexcept;
    UsbBulkDevice& device();

private:
    class Impl;

    UsbBulkAdapter(wl_ctx_t& link, UsbBulkDevice&& device,
                   std::size_t maximum_read_size, UsbBulkWakePolicy wake_policy,
                   UsbBulkAdapterConfig::ActivityCallback activity_callback,
                   void* activity_user_data);
    int start();
    static wl_sink_result_t sink(void* user_data, wl_io_token_t token,
                                 const uint8_t* data, size_t length);

    std::unique_ptr<Impl> m_impl;
};
} // namespace wirelink::astrial

#endif // WIRELINK_ASTRIAL_USB_BULK_ADAPTER_HPP_

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
#include "wirelink/usb_bulk_defaults.h"

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
    // Apply this value before link init and prepare caller-owned native RX
    // storage with wl_usb_bulk_rx_init(). open() only validates that setup.
    // Capacity and slots are required application budgets; zero is invalid.
    wl_usb_bulk_config_t transport{};
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
    wl_usb_bulk_rx_fault_t rx_fault{WL_USB_BULK_RX_FAULT_NONE};
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
    [[nodiscard]] wl_readiness_t readiness() const noexcept;
    // Sticky until this adapter is closed. On a fault: quiesce, reset or
    // reconfigure USB (or physically reconnect), then reopen. Reopen alone
    // does not establish a fresh boundary on the remote USB endpoint.
    [[nodiscard]] wl_usb_bulk_rx_fault_t rx_fault() const noexcept;
    // Blocks the single-consumer context until RX/TX activity or timeout.
    // Notifications coalesce into at most one pending wake even when only the
    // external activity callback is used. This does not count RX/TX units.
    bool wait_for_activity(std::chrono::nanoseconds timeout);
    void get_stats(UsbBulkAdapterStats& out_stats) const;
    void get_common_stats(wl_adapter_stats_t& out_stats) const noexcept;
    // The adapter owns the device disconnect callback for fault publication;
    // do not replace it. Use activity_callback and rx_fault()/stats instead.
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

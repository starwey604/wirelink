/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/astrial/usb_bulk_adapter.hpp"
#include "wirelink/diagnostics/host_profile.hpp"
#include "wirelink/detail/coalescing_event.hpp"
#include "wirelink/frame.h"

#include <atomic>
#include <span>
#include <utility>

namespace wirelink::astrial
{
using diagnostics::Scope;
using diagnostics::Stage;
namespace
{
enum class TxCompletion : unsigned int { None, Done, Failed };
}

class UsbBulkAdapter::Impl
{
public:
    Impl(wl_ctx_t& link_context, UsbBulkDevice&& usb_device,
         std::size_t read_size, UsbBulkWakePolicy policy,
         UsbBulkAdapterConfig::ActivityCallback callback,
         void* callback_user_data)
        : link(link_context), device(std::move(usb_device)),
          maximum_read_size(read_size), wake_policy(policy),
          activity_callback(callback), activity_user_data(callback_user_data)
    {
    }

    UsbBorrowedBuffer acquire_rx_buffer()
    {
        // Native framing cannot resynchronize a suffix after a failed/full
        // request. The application must reset the transport and reopen.
        if (rx_fault.load(std::memory_order_acquire) != WL_USB_BULK_RX_FAULT_NONE) return {};
        if (claim_active)
        {
            errors.fetch_add(1, std::memory_order_relaxed);
            pause_rx();
            return {};
        }

        wl_rx_unit_claim_t next_unit{};
        const int result = wl_rx_unit_claim(&link, maximum_read_size, &next_unit);
        if (result != WL_OK)
        {
            rx_pauses.fetch_add(1, std::memory_order_relaxed);
            pause_rx();
            return {};
        }
        unit_claim = next_unit;
        claim_active = true;
        rx_paused.store(false, std::memory_order_release);
        rx_claims.fetch_add(1, std::memory_order_relaxed);
        const auto& active = unit_claim.span;
        return {{active.data, active.length},
                unit_claim.token};
    }

    void complete_rx(const std::error_code& error, UsbBorrowedBuffer buffer,
                     std::size_t length)
    {
        Scope profile(Stage::usb_rx);
        const auto now = diagnostics::timestamp();
        diagnostics::record(Stage::usb_rx_gap, last_rx_ns, now);
        last_rx_ns = now;
        const auto& active = unit_claim.span;
        const auto active_token = unit_claim.token;
        if (!claim_active || buffer.token != active_token ||
            buffer.bytes.data() != active.data || length > active.length)
        {
            latch_rx_fault(WL_USB_BULK_RX_FAULT_INVALID_COMPLETION);
            if (claim_active)
            {
                (void)wl_rx_unit_abort(&link, &unit_claim);
                claim_active = false;
                unit_claim = {};
            }
            errors.fetch_add(1, std::memory_order_relaxed);
            notify_activity();
            return;
        }

        const bool cancelled_at_stop = error == make_error_code(UsbError::TransferCancelled) &&
                                       stopping.load(std::memory_order_acquire);
        const bool boundary_lost = error || length >= active.length;
        if (boundary_lost || length == 0)
        {
            (void)wl_rx_unit_abort(&link, &unit_claim);
            claim_active = false;
            unit_claim = {};
            if (boundary_lost && !cancelled_at_stop)
            {
                latch_rx_fault(error ? WL_USB_BULK_RX_FAULT_TRANSFER
                                     : WL_USB_BULK_RX_FAULT_BOUNDARY_LOST);
                errors.fetch_add(1, std::memory_order_relaxed);
            }
            notify_activity();
            return;
        }
        if (wl_rx_unit_commit(&link, &unit_claim, length) != WL_OK)
        {
            (void)wl_rx_unit_abort(&link, &unit_claim);
            latch_rx_fault(WL_USB_BULK_RX_FAULT_QUEUE);
            claim_active = false;
            unit_claim = {};
            errors.fetch_add(1, std::memory_order_relaxed);
            notify_activity();
            return;
        }
        claim_active = false;
        unit_claim = {};
        rx_bytes.fetch_add(length, std::memory_order_relaxed);
        rx_completions.fetch_add(1, std::memory_order_relaxed);
        rx_profile.notify();
        notify_activity();
    }

    void record_tx_completion(const std::error_code& error,
                              std::size_t transferred, std::size_t expected)
    {
        const TxCompletion wanted = (!error && transferred == expected)
                                        ? TxCompletion::Done
                                        : TxCompletion::Failed;
        TxCompletion none = TxCompletion::None;
        if (!tx_completion.compare_exchange_strong(none, wanted,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed))
        {
            errors.fetch_add(1, std::memory_order_relaxed);
        }
        if (wanted == TxCompletion::Done)
            tx_bytes.fetch_add(transferred, std::memory_order_relaxed);
        if (wake_policy == UsbBulkWakePolicy::AllCompletions) notify_activity();
    }

    void pause_rx()
    {
        if (!rx_paused.exchange(true, std::memory_order_acq_rel))
        {
            notify_activity();
        }
    }

    void latch_rx_fault(wl_usb_bulk_rx_fault_t reason)
    {
        auto expected = WL_USB_BULK_RX_FAULT_NONE;
        if (rx_fault.compare_exchange_strong(expected, reason,
                                             std::memory_order_acq_rel))
            fault_pending.store(true, std::memory_order_release);
    }

    void notify_activity()
    {
        activity_notifications.fetch_add(1, std::memory_order_relaxed);
        if (activity_callback != nullptr)
            activity_callback(activity_user_data);
        activity.notify();
    }

    bool wait_for_activity(std::chrono::nanoseconds timeout)
    {
        wait_calls.fetch_add(1, std::memory_order_relaxed);
        if (!activity.wait_for(timeout))
        {
            wait_timeouts.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        wait_wakeups.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    wl_ctx_t& link;
    UsbBulkDevice device;
    std::size_t maximum_read_size{};
    UsbBulkWakePolicy wake_policy{UsbBulkWakePolicy::AllCompletions};
    UsbBulkAdapterConfig::ActivityCallback activity_callback{};
    void* activity_user_data{};
    std::atomic<bool> started{false};
    std::atomic<bool> stopping{false};
    std::uint64_t last_rx_ns{}; // USB event producer only; profiling clock, not protocol time.
    diagnostics::PendingTimestamp rx_profile;
    std::atomic<bool> rx_paused{false};
    std::atomic<wl_usb_bulk_rx_fault_t> rx_fault{WL_USB_BULK_RX_FAULT_NONE};
    std::atomic<bool> fault_pending{false};
    bool claim_active{};
    wl_rx_unit_claim_t unit_claim{};
    std::atomic<bool> tx_active{false};
    std::atomic<TxCompletion> tx_completion{TxCompletion::None};
    wirelink::detail::CoalescingEvent activity;
    wl_io_token_t tx_token{};
    std::atomic<uint64_t> rx_claims{};
    std::atomic<uint64_t> rx_bytes{};
    std::atomic<uint64_t> rx_completions{};
    std::atomic<uint64_t> rx_pauses{};
    std::atomic<uint64_t> tx_submissions{};
    std::atomic<uint64_t> tx_completions{};
    std::atomic<uint64_t> tx_bytes{};
    std::atomic<uint64_t> activity_notifications{};
    std::atomic<uint64_t> wait_calls{};
    std::atomic<uint64_t> wait_wakeups{};
    std::atomic<uint64_t> wait_timeouts{};
    std::atomic<uint64_t> service_calls{};
    std::atomic<uint64_t> errors{};
};

UsbBulkAdapter::UsbBulkAdapter(wl_ctx_t& link, UsbBulkDevice&& device,
                               std::size_t maximum_read_size,
                               UsbBulkWakePolicy wake_policy,
                               UsbBulkAdapterConfig::ActivityCallback activity_callback,
                               void* activity_user_data)
    : m_impl(std::make_unique<Impl>(link, std::move(device), maximum_read_size,
                                    wake_policy, activity_callback,
                                    activity_user_data))
{
}

UsbBulkAdapter::~UsbBulkAdapter()
{
    if (!m_impl) return;
    quiesce();
}

tl::expected<std::unique_ptr<UsbBulkAdapter>, std::error_code>
UsbBulkAdapter::open(wl_ctx_t& link, const UsbBulkAdapterConfig& config)
{
    if (wl_usb_bulk_config_check_rx(&link, config.transport) != WL_OK)
    {
        return tl::make_unexpected(make_error_code(UsbError::InvalidArgument));
    }

    // A short USB transfer can only finish the latest direct ring claim.
    // Force one outstanding transfer so a successor can never leave a hole.
    auto usb_config = config.usb;
    usb_config.read_queue_depth = 1;
    auto opened = UsbBulkDevice::open(usb_config);
    if (!opened) return tl::make_unexpected(opened.error());
    if (!opened->endpoint_info().maximum_packet_size_in ||
        config.transport.rx_capacity < opened->endpoint_info().maximum_packet_size_in)
        return tl::make_unexpected(make_error_code(UsbError::InvalidArgument));

    auto adapter = std::unique_ptr<UsbBulkAdapter>(new UsbBulkAdapter(
        link, std::move(opened.value()), config.transport.rx_capacity,
        config.wake_policy, config.activity_callback,
        config.activity_user_data));
    if (adapter->start() != WL_OK)
    {
        return tl::make_unexpected(make_error_code(UsbError::InvalidArgument));
    }
    return adapter;
}

int UsbBulkAdapter::start()
{
    const int result = wl_set_sink(&m_impl->link, sink, this);
    if (result != WL_OK) return result;

    m_impl->started.store(true, std::memory_order_release);
    m_impl->device.on_disconnect([this](const std::error_code&) {
        if (!m_impl->stopping.load(std::memory_order_acquire)) {
            m_impl->latch_rx_fault(WL_USB_BULK_RX_FAULT_DISCONNECTED);
            m_impl->errors.fetch_add(1, std::memory_order_relaxed);
            m_impl->notify_activity();
        }
    });
    auto read_result = m_impl->device.start_reads_borrowed(
        [this] { return m_impl->acquire_rx_buffer(); },
        [this](const std::error_code& error, UsbBorrowedBuffer buffer,
               std::size_t length)
        {
            m_impl->complete_rx(error, buffer, length);
        });
    if (!read_result)
    {
        m_impl->started.store(false, std::memory_order_release);
        (void)wl_set_sink(&m_impl->link, nullptr, nullptr);
        return WL_ERR_IO;
    }
    return WL_OK;
}

wl_sink_result_t UsbBulkAdapter::sink(void* user_data, wl_io_token_t token,
                                      const uint8_t* data, size_t length)
{
    Scope profile(Stage::usb_tx);
    auto* adapter = static_cast<UsbBulkAdapter*>(user_data);
    if (adapter == nullptr || data == nullptr || length == 0)
    {
        return WL_SINK_FAILED;
    }
    auto& impl = *adapter->m_impl;
    if (!impl.started.load(std::memory_order_acquire) ||
        impl.stopping.load(std::memory_order_acquire))
    {
        return WL_SINK_FAILED;
    }

    bool inactive = false;
    if (!impl.tx_active.compare_exchange_strong(inactive, true,
                                                std::memory_order_acq_rel,
                                                std::memory_order_relaxed))
    {
        return WL_SINK_BUSY;
    }

    impl.tx_token = token;
    impl.tx_completion.store(TxCompletion::None, std::memory_order_relaxed);
    const auto result = impl.device.async_write_borrowed(
        {data, length},
        [adapter, length](const std::error_code& error, std::size_t transferred)
        {
            adapter->m_impl->record_tx_completion(error, transferred, length);
        });
    if (!result)
    {
        impl.tx_token = 0;
        impl.tx_active.store(false, std::memory_order_release);
        if (result.error() == make_error_code(UsbError::InterfaceBusy))
        {
            return WL_SINK_BUSY;
        }
        impl.errors.fetch_add(1, std::memory_order_relaxed);
        return WL_SINK_FAILED;
    }
    impl.tx_submissions.fetch_add(1, std::memory_order_relaxed);
    return WL_SINK_STARTED;
}

int UsbBulkAdapter::service()
{
    if (m_impl) m_impl->rx_profile.consume(Stage::rx_to_owner);
    if (!m_impl) return WL_ERR_INVALID_ARG;
    m_impl->service_calls.fetch_add(1, std::memory_order_relaxed);

    const TxCompletion completion =
        m_impl->tx_completion.exchange(TxCompletion::None, std::memory_order_acq_rel);
    if (completion != TxCompletion::None)
    {
        const wl_io_token_t token = m_impl->tx_token;
        m_impl->tx_token = 0;
        m_impl->tx_active.store(false, std::memory_order_release);
        m_impl->tx_completions.fetch_add(1, std::memory_order_relaxed);
        const int result = wl_tx_consumed(
            &m_impl->link, token,
            completion == TxCompletion::Done ? WL_OK : WL_ERR_IO);
        if (result != WL_OK)
        {
            m_impl->errors.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
    }

    if (m_impl->rx_paused.exchange(false, std::memory_order_acq_rel) &&
        !m_impl->stopping.load(std::memory_order_acquire) &&
        m_impl->rx_fault.load(std::memory_order_acquire) == WL_USB_BULK_RX_FAULT_NONE)
    {
        m_impl->device.resume_reads();
    }
    m_impl->fault_pending.store(false, std::memory_order_release);
    return m_impl->rx_fault.load(std::memory_order_acquire) == WL_USB_BULK_RX_FAULT_NONE
               ? WL_OK : WL_ERR_IO;
}

void UsbBulkAdapter::quiesce() noexcept
{
    if (!m_impl || m_impl->stopping.exchange(true, std::memory_order_acq_rel))
        return;
    m_impl->device.close();
    (void)service();
    if (m_impl->claim_active)
    {
        (void)wl_rx_unit_abort(&m_impl->link, &m_impl->unit_claim);
        m_impl->claim_active = false;
    }
    (void)wl_set_sink(&m_impl->link, nullptr, nullptr);
    m_impl->started.store(false, std::memory_order_release);
}

wl_readiness_t UsbBulkAdapter::readiness() const noexcept
{
    wl_readiness_t value = wl_readiness_idle();
    if (!m_impl || m_impl->stopping.load(std::memory_order_acquire)) return value;
    value.runnable = m_impl->fault_pending.load(std::memory_order_acquire) ||
        m_impl->tx_completion.load(std::memory_order_acquire) != TxCompletion::None ||
        (m_impl->rx_fault.load(std::memory_order_acquire) == WL_USB_BULK_RX_FAULT_NONE &&
         m_impl->rx_paused.load(std::memory_order_acquire) &&
         wl_rx_can_accept_unit(&m_impl->link));
    value.blocked = m_impl->tx_active.load(std::memory_order_acquire) ||
        m_impl->rx_paused.load(std::memory_order_acquire);
    return value;
}

wl_usb_bulk_rx_fault_t UsbBulkAdapter::rx_fault() const noexcept
{
    return m_impl->rx_fault.load(std::memory_order_acquire);
}

bool UsbBulkAdapter::wait_for_activity(std::chrono::nanoseconds timeout)
{
    if (!m_impl) return false;
    return m_impl->wait_for_activity(timeout);
}

void UsbBulkAdapter::get_stats(UsbBulkAdapterStats& out_stats) const
{
    out_stats.rx_claims = m_impl->rx_claims.load(std::memory_order_relaxed);
    out_stats.rx_bytes = m_impl->rx_bytes.load(std::memory_order_relaxed);
    out_stats.rx_pauses = m_impl->rx_pauses.load(std::memory_order_relaxed);
    out_stats.tx_submissions = m_impl->tx_submissions.load(std::memory_order_relaxed);
    out_stats.tx_completions = m_impl->tx_completions.load(std::memory_order_relaxed);
    out_stats.activity_notifications =
        m_impl->activity_notifications.load(std::memory_order_relaxed);
    out_stats.wait_calls = m_impl->wait_calls.load(std::memory_order_relaxed);
    out_stats.wait_wakeups = m_impl->wait_wakeups.load(std::memory_order_relaxed);
    out_stats.wait_timeouts = m_impl->wait_timeouts.load(std::memory_order_relaxed);
    out_stats.errors = m_impl->errors.load(std::memory_order_relaxed);
    out_stats.started = m_impl->started.load(std::memory_order_relaxed);
    out_stats.rx_paused = m_impl->rx_paused.load(std::memory_order_relaxed);
    out_stats.tx_active = m_impl->tx_active.load(std::memory_order_relaxed);
    out_stats.rx_fault = rx_fault();
}

void UsbBulkAdapter::get_common_stats(
    wl_adapter_stats_t& out_stats) const noexcept
{
    out_stats = wl_adapter_stats_t{
        .rx_units = m_impl->rx_completions.load(std::memory_order_relaxed),
        .rx_bytes = m_impl->rx_bytes.load(std::memory_order_relaxed),
        .rx_backpressure = m_impl->rx_pauses.load(std::memory_order_relaxed),
        .tx_units = m_impl->tx_submissions.load(std::memory_order_relaxed),
        .tx_bytes = m_impl->tx_bytes.load(std::memory_order_relaxed),
        .tx_completions = m_impl->tx_completions.load(std::memory_order_relaxed),
        .activity_notifications =
            m_impl->activity_notifications.load(std::memory_order_relaxed),
        .service_calls = m_impl->service_calls.load(std::memory_order_relaxed),
        .errors = m_impl->errors.load(std::memory_order_relaxed),
        .started = static_cast<std::uint8_t>(
            m_impl->started.load(std::memory_order_relaxed)),
        .rx_paused = static_cast<std::uint8_t>(
            m_impl->rx_paused.load(std::memory_order_relaxed)),
        .tx_active = static_cast<std::uint8_t>(
            m_impl->tx_active.load(std::memory_order_relaxed)),
    };
}

UsbBulkDevice& UsbBulkAdapter::device()
{
    return m_impl->device;
}
} // namespace wirelink::astrial

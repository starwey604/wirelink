/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/astrial/usb_bulk_adapter.hpp"

#include <atomic>
#include <climits>
#include <semaphore>
#include <span>
#include <utility>
#include <vector>

namespace wirelink::astrial
{
namespace
{
enum class TxCompletion : unsigned int { None, Done, Failed };
}

class UsbBulkAdapter::Impl
{
public:
    Impl(wl_ctx_t& link_context, UsbBulkDevice&& usb_device,
         std::size_t read_size, UsbBulkWakePolicy policy)
        : link(link_context), device(std::move(usb_device)),
          maximum_read_size(read_size), wake_policy(policy)
    {
    }

    UsbBorrowedBuffer acquire_rx_buffer()
    {
        if (claim_active)
        {
            errors.fetch_add(1, std::memory_order_relaxed);
            pause_rx();
            return {};
        }

        wl_rx_dma_claim_t next{};
        wl_rx_unit_claim_t next_unit{};
        const int result = native_unit_mode
                               ? wl_rx_unit_claim(&link, maximum_read_size,
                                                  &next_unit)
                               : wl_rx_dma_claim(&link, maximum_read_size,
                                                 &next);
        if (result != WL_OK)
        {
            rx_pauses.fetch_add(1, std::memory_order_relaxed);
            pause_rx();
            return {};
        }
        // A short tail claim is not a safe USB transfer buffer: the host
        // controller may receive a full endpoint packet and report overflow
        // before Wirelink gets a chance to wrap the ring. Pause until the
        // consumer drains the ring, allowing the next claim to normalize at
        // physical offset zero.
        if (!native_unit_mode && next.span.length < maximum_read_size)
        {
            if (wl_rx_dma_finish(&link, &next) != WL_OK)
            {
                (void)wl_rx_dma_abort(&link);
                errors.fetch_add(1, std::memory_order_relaxed);
            }
            rx_pauses.fetch_add(1, std::memory_order_relaxed);
            pause_rx();
            return {};
        }

        claim = next;
        unit_claim = next_unit;
        claim_active = true;
        rx_paused.store(false, std::memory_order_release);
        rx_claims.fetch_add(1, std::memory_order_relaxed);
        const auto& active = native_unit_mode ? unit_claim.span : claim.span;
        return {{active.data, active.length},
                native_unit_mode ? unit_claim.token : claim.token};
    }

    void complete_rx(const std::error_code& error, UsbBorrowedBuffer buffer,
                     std::size_t length)
    {
        const auto& active = native_unit_mode ? unit_claim.span : claim.span;
        const auto active_token = native_unit_mode ? unit_claim.token : claim.token;
        if (!claim_active || buffer.token != active_token ||
            buffer.bytes.data() != active.data || length > active.length)
        {
            if (claim_active)
            {
                if (native_unit_mode)
                    (void)wl_rx_unit_abort(&link, &unit_claim);
                else
                    (void)wl_rx_dma_abort(&link);
                claim_active = false;
                claim = {};
                unit_claim = {};
            }
            errors.fetch_add(1, std::memory_order_relaxed);
            notify_activity();
            return;
        }

        bool failed = false;
        if (native_unit_mode)
            failed = error || length == 0 ||
                     wl_rx_unit_commit(&link, &unit_claim, length) != WL_OK;
        else
        {
            if (length > 0 && wl_rx_dma_publish(&link, &claim, 0, length) != WL_OK)
                failed = true;
            if (!failed && wl_rx_dma_finish(&link, &claim) != WL_OK)
                failed = true;
        }

        if (failed)
        {
            if (native_unit_mode)
                (void)wl_rx_unit_abort(&link, &unit_claim);
            else
                (void)wl_rx_dma_abort(&link);
            claim_active = false;
            claim = {};
            unit_claim = {};
            errors.fetch_add(1, std::memory_order_relaxed);
            notify_activity();
            return;
        }
        claim_active = false;
        claim = {};
        unit_claim = {};
        rx_bytes.fetch_add(length, std::memory_order_relaxed);
        if (error && !stopping.load(std::memory_order_acquire) &&
            error != make_error_code(UsbError::TransferCancelled))
        {
            errors.fetch_add(1, std::memory_order_relaxed);
        }
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
        if (wake_policy == UsbBulkWakePolicy::AllCompletions) notify_activity();
    }

    void pause_rx()
    {
        if (!rx_paused.exchange(true, std::memory_order_acq_rel))
        {
            notify_activity();
        }
    }

    void notify_activity()
    {
        activity_notifications.fetch_add(1, std::memory_order_relaxed);
        activity.release();
    }

    bool wait_for_activity(std::chrono::nanoseconds timeout)
    {
        wait_calls.fetch_add(1, std::memory_order_relaxed);
        if (!activity.try_acquire_for(timeout))
        {
            wait_timeouts.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        while (activity.try_acquire())
        {
        }
        wait_wakeups.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    wl_ctx_t& link;
    UsbBulkDevice device;
    std::size_t maximum_read_size{};
    UsbBulkWakePolicy wake_policy{UsbBulkWakePolicy::AllCompletions};
    std::atomic<bool> started{false};
    std::atomic<bool> stopping{false};
    std::atomic<bool> rx_paused{false};
    bool claim_active{};
    bool native_unit_mode{};
    wl_rx_dma_claim_t claim{};
    wl_rx_unit_claim_t unit_claim{};
    std::vector<std::uint8_t> unit_storage;
    std::atomic<bool> tx_active{false};
    std::atomic<TxCompletion> tx_completion{TxCompletion::None};
    std::counting_semaphore<INT_MAX> activity{0};
    wl_io_token_t tx_token{};
    std::atomic<uint64_t> rx_claims{};
    std::atomic<uint64_t> rx_bytes{};
    std::atomic<uint64_t> rx_pauses{};
    std::atomic<uint64_t> tx_submissions{};
    std::atomic<uint64_t> tx_completions{};
    std::atomic<uint64_t> activity_notifications{};
    std::atomic<uint64_t> wait_calls{};
    std::atomic<uint64_t> wait_wakeups{};
    std::atomic<uint64_t> wait_timeouts{};
    std::atomic<uint64_t> errors{};
};

UsbBulkAdapter::UsbBulkAdapter(wl_ctx_t& link, UsbBulkDevice&& device,
                               std::size_t maximum_read_size,
                               UsbBulkWakePolicy wake_policy)
    : m_impl(std::make_unique<Impl>(link, std::move(device), maximum_read_size,
                                    wake_policy))
{
}

UsbBulkAdapter::~UsbBulkAdapter()
{
    if (!m_impl) return;

    m_impl->stopping.store(true, std::memory_order_release);
    m_impl->device.close();
    (void)service();
    if (m_impl->claim_active)
    {
        if (m_impl->native_unit_mode)
            (void)wl_rx_unit_abort(&m_impl->link, &m_impl->unit_claim);
        else
            (void)wl_rx_dma_abort(&m_impl->link);
        m_impl->claim_active = false;
    }
    (void)wl_set_sink(&m_impl->link, nullptr, nullptr);
    m_impl->started.store(false, std::memory_order_release);
}

tl::expected<std::unique_ptr<UsbBulkAdapter>, std::error_code>
UsbBulkAdapter::open(wl_ctx_t& link, const UsbBulkAdapterConfig& config)
{
    wl_config_t link_config{};
    if (wl_get_config(&link, &link_config) != WL_OK ||
        (link_config.envelope != WL_ENVELOPE_COBS_STREAM &&
         link_config.envelope != WL_ENVELOPE_NATIVE_PACKET) ||
        config.maximum_read_size == 0 || config.unit_queue_slots < 2 ||
        config.unit_queue_slots > WL_RX_UNIT_QUEUE_MAX_SLOTS)
    {
        return tl::make_unexpected(make_error_code(UsbError::InvalidArgument));
    }

    // A short USB transfer can only finish the latest direct ring claim.
    // Force one outstanding transfer so a successor can never leave a hole.
    auto usb_config = config.usb;
    usb_config.read_queue_depth = 1;
    auto opened = UsbBulkDevice::open(usb_config);
    if (!opened) return tl::make_unexpected(opened.error());

    auto adapter = std::unique_ptr<UsbBulkAdapter>(new UsbBulkAdapter(
        link, std::move(opened.value()), config.maximum_read_size,
        config.wake_policy));
    if (link_config.envelope == WL_ENVELOPE_NATIVE_PACKET)
    {
        adapter->m_impl->native_unit_mode = true;
        adapter->m_impl->unit_storage.resize(config.maximum_read_size *
                                             config.unit_queue_slots);
        const wl_rx_unit_queue_config_t queue_config{
            .storage = adapter->m_impl->unit_storage.data(),
            .storage_size = adapter->m_impl->unit_storage.size(),
            .unit_size = config.maximum_read_size,
            .slot_count = config.unit_queue_slots,
        };
        if (wl_rx_unit_queue_init(&link, &queue_config) != WL_OK)
            return tl::make_unexpected(make_error_code(UsbError::InvalidArgument));
    }
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
    auto read_result = m_impl->device.start_reads(
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
    if (!m_impl) return WL_ERR_INVALID_ARG;

    const TxCompletion completion =
        m_impl->tx_completion.exchange(TxCompletion::None, std::memory_order_acq_rel);
    if (completion != TxCompletion::None)
    {
        const wl_io_token_t token = m_impl->tx_token;
        m_impl->tx_token = 0;
        m_impl->tx_active.store(false, std::memory_order_release);
        m_impl->tx_completions.fetch_add(1, std::memory_order_relaxed);
        const int result = wl_tx_complete(
            &m_impl->link, token,
            completion == TxCompletion::Done ? WL_OK : WL_ERR_IO);
        if (result != WL_OK)
        {
            m_impl->errors.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
    }

    if (m_impl->rx_paused.exchange(false, std::memory_order_acq_rel) &&
        !m_impl->stopping.load(std::memory_order_acquire))
    {
        m_impl->device.resume_reads();
    }
    return WL_OK;
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
}

UsbBulkDevice& UsbBulkAdapter::device()
{
    return m_impl->device;
}
} // namespace wirelink::astrial

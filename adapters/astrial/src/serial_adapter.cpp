/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/astrial/serial_adapter.hpp"

#include <atomic>
#include <span>
#include <utility>

namespace wirelink::astrial
{
namespace
{
enum class TxCompletion : unsigned int { None, Done, Failed };
}

class SerialAdapter::Impl
{
public:
    Impl(wl_ctx_t& link_context, Serial&& serial_port)
        : link(link_context), port(std::move(serial_port))
    {
    }

    std::span<uint8_t> acquire_rx_buffer()
    {
        wl_span_t reservation{};
        const int result = wl_rx_reserve(&link, &reservation);
        if (result != WL_OK)
        {
            errors.fetch_add(1, std::memory_order_relaxed);
            rx_paused.store(true, std::memory_order_release);
            return {};
        }
        if (reservation.length == 0)
        {
            (void)wl_rx_commit(&link, 0);
            rx_pauses.fetch_add(1, std::memory_order_relaxed);
            rx_paused.store(true, std::memory_order_release);
            return {};
        }

        rx_reservation_length = reservation.length;
        rx_reservation_active = true;
        rx_paused.store(false, std::memory_order_release);
        rx_reservations.fetch_add(1, std::memory_order_relaxed);
        return {reservation.data, reservation.length};
    }

    void complete_rx(const std::error_code& error, std::size_t length)
    {
        if (!rx_reservation_active || length > rx_reservation_length)
        {
            errors.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        rx_reservation_active = false;
        rx_reservation_length = 0;
        if (wl_rx_commit(&link, length) != WL_OK)
        {
            errors.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        rx_bytes.fetch_add(length, std::memory_order_relaxed);
        if (error && !stopping.load(std::memory_order_acquire))
        {
            errors.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void record_tx_completion(const std::error_code& error,
                              std::size_t transferred, std::size_t expected)
    {
        TxCompletion wanted = (!error && transferred == expected)
                                  ? TxCompletion::Done
                                  : TxCompletion::Failed;
        TxCompletion none = TxCompletion::None;
        if (!tx_completion.compare_exchange_strong(none, wanted,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed))
        {
            errors.fetch_add(1, std::memory_order_relaxed);
        }
    }

    wl_ctx_t& link;
    Serial port;
    std::atomic<bool> started{false};
    std::atomic<bool> stopping{false};
    std::atomic<bool> rx_paused{false};
    bool rx_reservation_active{};
    std::size_t rx_reservation_length{};
    std::atomic<bool> tx_active{false};
    std::atomic<TxCompletion> tx_completion{TxCompletion::None};
    wl_io_token_t tx_token{};
    std::atomic<uint64_t> rx_reservations{};
    std::atomic<uint64_t> rx_bytes{};
    std::atomic<uint64_t> rx_pauses{};
    std::atomic<uint64_t> tx_submissions{};
    std::atomic<uint64_t> tx_completions{};
    std::atomic<uint64_t> errors{};
};

SerialAdapter::SerialAdapter(wl_ctx_t& link, Serial&& serial)
    : m_impl(std::make_unique<Impl>(link, std::move(serial)))
{
}

SerialAdapter::~SerialAdapter()
{
    if (!m_impl) return;

    m_impl->stopping.store(true, std::memory_order_release);
    m_impl->port.close();
    (void)service();
    if (m_impl->link.sink_user_data == this)
    {
        (void)wl_set_sink(&m_impl->link, nullptr, nullptr);
    }
    m_impl->started.store(false, std::memory_order_release);
}

tl::expected<std::unique_ptr<SerialAdapter>, std::error_code>
SerialAdapter::open(wl_ctx_t& link, const SerialConfig& config)
{
    if (link.config == nullptr || config.port.empty() ||
        link.config->envelope != WL_ENVELOPE_COBS_STREAM)
    {
        return tl::make_unexpected(make_error_code(SerialError::InvalidArgument));
    }

    auto opened = Serial::builder()
                  .baud_rate(config.baud_rate)
                  .parity(config.parity)
                  .stop_bits(config.stop_bits)
                  .data_bits(config.data_bits)
                  .auto_reconnect(config.auto_reconnect, config.reconnect_interval)
                  .open(config.port);
    if (!opened) return tl::make_unexpected(opened.error());

    auto adapter = std::unique_ptr<SerialAdapter>(
        new SerialAdapter(link, std::move(opened.value())));
    if (adapter->start() != WL_OK)
    {
        return tl::make_unexpected(make_error_code(SerialError::InvalidArgument));
    }
    return adapter;
}

int SerialAdapter::start()
{
    const int result = wl_set_sink(&m_impl->link, sink, this);
    if (result != WL_OK) return result;

    m_impl->started.store(true, std::memory_order_release);
    m_impl->port.on_data_borrowed(
        [this] { return m_impl->acquire_rx_buffer(); },
        [this](const std::error_code& error, std::size_t length)
        {
            m_impl->complete_rx(error, length);
        });
    return WL_OK;
}

wl_sink_result_t SerialAdapter::sink(void* user_data, wl_io_token_t token,
                                     const uint8_t* data, size_t length)
{
    auto* adapter = static_cast<SerialAdapter*>(user_data);
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
    impl.tx_submissions.fetch_add(1, std::memory_order_relaxed);
    impl.port.async_write_borrowed(
        {data, length},
        [adapter, length](const std::error_code& error, std::size_t transferred)
        {
            adapter->m_impl->record_tx_completion(error, transferred, length);
        });
    return WL_SINK_STARTED;
}

int SerialAdapter::service()
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
        m_impl->port.resume_read();
    }
    return WL_OK;
}

void SerialAdapter::get_stats(SerialAdapterStats& out_stats) const
{
    out_stats.rx_reservations = m_impl->rx_reservations.load(std::memory_order_relaxed);
    out_stats.rx_bytes = m_impl->rx_bytes.load(std::memory_order_relaxed);
    out_stats.rx_pauses = m_impl->rx_pauses.load(std::memory_order_relaxed);
    out_stats.tx_submissions = m_impl->tx_submissions.load(std::memory_order_relaxed);
    out_stats.tx_completions = m_impl->tx_completions.load(std::memory_order_relaxed);
    out_stats.errors = m_impl->errors.load(std::memory_order_relaxed);
    out_stats.started = m_impl->started.load(std::memory_order_relaxed);
    out_stats.rx_paused = m_impl->rx_paused.load(std::memory_order_relaxed);
    out_stats.tx_active = m_impl->tx_active.load(std::memory_order_relaxed);
}

Serial& SerialAdapter::serial()
{
    return m_impl->port;
}
}

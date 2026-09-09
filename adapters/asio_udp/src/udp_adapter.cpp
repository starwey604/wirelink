/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/asio/udp_adapter.hpp"
#include <asio.hpp>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <mswsock.h>
#endif

namespace wirelink::asio
{
class UdpAdapter::Impl
{
public:
    Impl(wl_ctx_t& context, const UdpAdapterConfig& settings,
         std::size_t maximum, bool stream_mode, std::size_t frame_bound)
        : link(context), socket(io), config(settings), maximum_datagram_size(maximum),
          stream(stream_mode), stream_frame_bound(frame_bound),
          storage((maximum + 1) * (stream ? 1 : settings.receive_slots)) {}

    wl_ctx_t& link;
    wl_endpoint_t* endpoint{};
    ::asio::io_context io;
    ::asio::ip::udp::socket socket;
    ::asio::ip::udp::endpoint peer;
    UdpAdapterConfig config;
    std::size_t maximum_datagram_size;
    bool stream;
    std::size_t stream_frame_bound;
    std::vector<std::uint8_t> storage;
    std::size_t pending_stream_size{};
    bool have_peer{};
    bool quiesced{};
    bool more_pending{};
    bool tx_blocked{};
    UdpAdapterStats stats{};
    std::atomic<bool> wake_pending{false};

    int flush_stream()
    {
        if (pending_stream_size == 0) return WL_OK;
        wl_rx_dma_claim_t claim{};
        const int result = wl_rx_dma_claim(&link, pending_stream_size, &claim);
        if (result != WL_OK) return result;
        if (claim.span.length < pending_stream_size)
        {
            (void)wl_rx_dma_finish(&link, &claim);
            return WL_ERR_WOULD_BLOCK;
        }
        std::memcpy(claim.span.data, storage.data(), pending_stream_size);
        const int published = wl_rx_dma_publish(&link, &claim, 0, pending_stream_size);
        const int finished = wl_rx_dma_finish(&link, &claim);
        pending_stream_size = 0;
        return published != WL_OK ? published : finished;
    }
};

UdpAdapter::UdpAdapter(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

UdpAdapter::~UdpAdapter()
{
    if (!m_impl) return;
    if (m_impl->endpoint != nullptr) wl_endpoint_close(m_impl->endpoint);
    quiesce();
}

std::unique_ptr<UdpAdapter> UdpAdapter::open(wl_ctx_t& link,
                                             const UdpAdapterConfig& config,
                                             std::error_code& error)
{
    wl_config_t link_config{};
    wl_storage_requirements_t requirements{};
    error.clear();
    if (config.poll_interval.count() <= 0 || config.service_budget == 0 ||
        config.receive_slots < 2 || config.receive_slots > WL_RX_UNIT_QUEUE_MAX_SLOTS ||
        wl_get_config(&link, &link_config) != WL_OK ||
        (link_config.envelope != WL_ENVELOPE_COBS_STREAM &&
         link_config.envelope != WL_ENVELOPE_NATIVE_PACKET) ||
        wl_config_requirements(&link_config, &requirements) != WL_OK)
    {
        error = std::make_error_code(std::errc::invalid_argument);
        return nullptr;
    }
    const auto maximum = config.maximum_datagram_size != 0
        ? config.maximum_datagram_size : requirements.tx_unit_size;
    if (maximum < requirements.tx_unit_size || maximum > 65507)
    {
        error = std::make_error_code(std::errc::message_size);
        return nullptr;
    }
    const auto address = ::asio::ip::make_address(config.bind_address, error);
    if (error) return nullptr;
    auto impl = std::make_unique<Impl>(link, config, maximum,
        link_config.envelope == WL_ENVELOPE_COBS_STREAM, requirements.tx_unit_size);
    impl->socket.open(address.is_v6() ? ::asio::ip::udp::v6() : ::asio::ip::udp::v4(), error);
    if (error) return nullptr;
#if defined(_WIN32)
    // A telemetry peer may not have bound its port yet, or may have closed it.
    // Do not turn an ICMP Port Unreachable for a previous datagram into a
    // fatal receive error. Wirelink's reliable/RPC deadlines still detect loss.
    BOOL report_port_unreachable = FALSE;
    DWORD returned = 0;
    if (::WSAIoctl(impl->socket.native_handle(), SIO_UDP_CONNRESET,
                  &report_port_unreachable, sizeof(report_port_unreachable),
                  nullptr, 0, &returned, nullptr, nullptr) == SOCKET_ERROR)
    {
        error = std::error_code(::WSAGetLastError(), std::system_category());
        return nullptr;
    }
#endif
    // No address reuse: an accidentally duplicated tutorial port must fail.
    impl->socket.bind({address, config.bind_port}, error);
    if (error) return nullptr;
    impl->socket.non_blocking(true, error);
    if (error) return nullptr;
    auto adapter = std::unique_ptr<UdpAdapter>(new UdpAdapter(std::move(impl)));
    if (!adapter->m_impl->stream)
    {
        const wl_rx_unit_queue_config_t queue{
            adapter->m_impl->storage.data(), adapter->m_impl->storage.size(),
            maximum + 1, config.receive_slots};
        if (wl_rx_unit_queue_init(&link, &queue) != WL_OK)
        {
            error = std::make_error_code(std::errc::invalid_argument);
            return nullptr;
        }
    }
    if (wl_set_sink(&link, sink, adapter.get()) != WL_OK)
    {
        error = std::make_error_code(std::errc::invalid_argument);
        return nullptr;
    }
    return adapter;
}

std::unique_ptr<UdpAdapter> UdpAdapter::open(wl_endpoint_t& endpoint,
                                             const UdpAdapterConfig& config,
                                             std::error_code& error)
{
    auto* link = wl_endpoint_link(&endpoint);
    if (link == nullptr || wl_endpoint_has_adapter(&endpoint))
    {
        error = std::make_error_code(std::errc::invalid_argument);
        return nullptr;
    }
    auto adapter = open(*link, config, error);
    if (!adapter) return nullptr;
    wl_pump_hooks_t hooks{};
    hooks.adapter_user_data = adapter.get();
    hooks.service = [](void* context) { return static_cast<UdpAdapter*>(context)->service(); };
    hooks.quiesce = [](void* context) {
        auto* self = static_cast<UdpAdapter*>(context);
        self->quiesce();
        self->m_impl->endpoint = nullptr;
    };
    hooks.adapter_deadline_hint = [](const void* context, wl_time_ms_t now) {
        return static_cast<const UdpAdapter*>(context)->deadline_hint(now);
    };
    if (wl_endpoint_attach(&endpoint, &hooks) != WL_OK)
    {
        error = std::make_error_code(std::errc::invalid_argument);
        return nullptr;
    }
    adapter->m_impl->endpoint = &endpoint;
    const wl_waiter_t waiter{
        [](void* context, uint32_t maximum_ms) -> wl_err_t {
            try {
                return static_cast<UdpAdapter*>(context)->wait_for_activity(
                    std::chrono::milliseconds(maximum_ms));
            } catch (...) { return WL_ERR_IO; }
        }, adapter.get(), [](void* context) { static_cast<UdpAdapter*>(context)->notify(); }};
    (void)wl_endpoint_set_waiter(&endpoint, &waiter);
    return adapter;
}

int UdpAdapter::set_peer(std::string_view address, std::uint16_t port)
{
    if (m_impl->quiesced) return WL_ERR_INVALID_STATE;
    if (port == 0) return WL_ERR_INVALID_ARG;
    std::error_code error;
    const auto parsed = ::asio::ip::make_address(std::string(address), error);
    if (error) return WL_ERR_INVALID_ARG;
    const auto local = m_impl->socket.local_endpoint(error);
    if (error || parsed.is_v6() != local.address().is_v6()) return WL_ERR_INVALID_ARG;
    const ::asio::ip::udp::endpoint peer{parsed, port};
    // A different peer requires a fresh link, not reuse of its RPC/session state.
    if (m_impl->have_peer && peer != m_impl->peer) return WL_ERR_INVALID_STATE;
    m_impl->peer = peer;
    m_impl->have_peer = true;
    return WL_OK;
}

wl_sink_result_t UdpAdapter::sink(void* user_data, wl_io_token_t token,
                                  const std::uint8_t* data, std::size_t length)
{
    (void)token;
    auto& impl = *static_cast<UdpAdapter*>(user_data)->m_impl;
    if (impl.quiesced || data == nullptr || length == 0 ||
        length > impl.maximum_datagram_size) return WL_SINK_FAILED;
    if (!impl.have_peer) return WL_SINK_BUSY;
    std::error_code error;
    const auto sent = impl.socket.send_to(::asio::buffer(data, length), impl.peer, 0, error);
    impl.tx_blocked = error == ::asio::error::would_block || error == ::asio::error::try_again;
    if (impl.tx_blocked) return WL_SINK_BUSY;
    if (error || sent != length)
    {
        ++impl.stats.errors;
        return WL_SINK_FAILED;
    }
    ++impl.stats.tx_datagrams;
    impl.stats.tx_bytes += sent;
    return WL_SINK_SENT;
}

int UdpAdapter::service()
{
    auto& impl = *m_impl;
    if (impl.quiesced) return WL_ERR_INVALID_STATE;
    ++impl.stats.service_calls;
    impl.more_pending = false;
    for (std::size_t index = 0; index < impl.config.service_budget; ++index)
    {
        if (impl.stream && impl.pending_stream_size != 0)
        {
            const int flushed = impl.flush_stream();
            if (flushed != WL_OK)
            {
                ++impl.stats.rx_pauses;
                impl.more_pending = true;
                return flushed;
            }
        }
        wl_rx_unit_claim_t claim{};
        auto* buffer = impl.storage.data();
        if (!impl.stream)
        {
            const int result = wl_rx_unit_claim(&impl.link, impl.maximum_datagram_size + 1, &claim);
            if (result != WL_OK)
            {
                ++impl.stats.rx_pauses;
                impl.more_pending = true;
                return result;
            }
            buffer = claim.span.data;
        }
        const auto abort = [&] {
            if (!impl.stream) (void)wl_rx_unit_abort(&impl.link, &claim);
        };
        ::asio::ip::udp::endpoint source;
        std::error_code error;
        const auto received = impl.socket.receive_from(
            ::asio::buffer(buffer, impl.maximum_datagram_size + 1), source, 0, error);
        if (error == ::asio::error::would_block || error == ::asio::error::try_again)
        {
            abort();
            return index == 0 ? WL_ERR_NO_DATA : WL_OK;
        }
        // One extra byte detects truncation on platforms that silently discard
        // the tail instead of reporting message_size. Never publish a prefix.
        if (error == ::asio::error::message_size || received > impl.maximum_datagram_size ||
            (!error && (received == 0 || (impl.stream && received > impl.stream_frame_bound))))
        {
            abort();
            ++impl.stats.rx_rejected;
            continue;
        }
        if (error)
        {
            abort();
            ++impl.stats.errors;
            return WL_ERR_IO;
        }
        if (!impl.have_peer && impl.config.learn_peer_from_first_datagram)
        {
            impl.peer = source;
            impl.have_peer = true;
            ++impl.stats.peer_learns;
        }
        if (!impl.have_peer || source != impl.peer)
        {
            abort();
            ++impl.stats.rx_rejected;
            continue;
        }
        if (impl.stream) impl.pending_stream_size = received;
        else if (wl_rx_unit_commit(&impl.link, &claim, received) != WL_OK)
        {
            abort();
            ++impl.stats.errors;
            return WL_ERR_IO;
        }
        ++impl.stats.rx_datagrams;
        impl.stats.rx_bytes += received;
    }
    impl.more_pending = true;
    return WL_OK;
}

void UdpAdapter::quiesce() noexcept
{
    if (!m_impl || m_impl->quiesced) return;
    m_impl->quiesced = true;
    (void)wl_set_sink(&m_impl->link, nullptr, nullptr);
    std::error_code ignored;
    m_impl->socket.close(ignored);
}

int UdpAdapter::wait_for_activity(std::chrono::milliseconds maximum_wait)
{
    auto& impl = *m_impl;
    if (impl.quiesced) return WL_ERR_INVALID_STATE;
    if (maximum_wait.count() < 0) return WL_ERR_INVALID_ARG;
    if (maximum_wait.count() == 0 || impl.more_pending) return WL_OK;
    ++impl.stats.wait_calls;
    bool ready = false;
    bool failed = false;
    unsigned pending = 0;
    const auto completion = [&](const std::error_code& error) {
        --pending;
        if (!error) ready = true;
        else if (error != ::asio::error::operation_aborted) failed = true;
    };
    const auto drain = [&] {
        std::error_code error;
        impl.socket.cancel(error);
        if (error) quiesce();
        // Drain cancelled handlers before stack references leave scope,
        // including when initiation of the second wait throws.
        // notify() may stop the IO context while cancellation is drained.
        // No handler may retain these stack references after return.
        while (pending != 0) {
            impl.io.restart();
            impl.io.poll();
        }
        return error;
    };
    try
    {
        impl.io.restart();
        impl.socket.async_wait(::asio::ip::udp::socket::wait_read, completion);
        ++pending;
        if (impl.tx_blocked) {
            impl.socket.async_wait(::asio::ip::udp::socket::wait_write, completion);
            ++pending;
        }
        // restart happens BEFORE consuming the latch. A notification before
        // this check is observed here; one after it interrupts run_one_for.
        if (impl.wake_pending.exchange(false, std::memory_order_acquire)) ready = true;
        else impl.io.run_one_for(maximum_wait);
    }
    catch (...)
    {
        (void)drain();
        throw;
    }
    const auto error = drain();
    if (impl.wake_pending.exchange(false, std::memory_order_acquire)) ready = true;
    if (failed || error) return WL_ERR_IO;
    if (ready) ++impl.stats.activity_notifications;
    else ++impl.stats.wait_timeouts;
    return ready ? WL_OK : WL_ERR_NO_DATA;
}

void UdpAdapter::notify() noexcept
{
    m_impl->wake_pending.store(true, std::memory_order_release);
    m_impl->io.stop();
}

std::uint32_t UdpAdapter::deadline_hint(wl_time_ms_t now_ms) const noexcept
{
    (void)now_ms;
    if (m_impl->quiesced) return WL_POLL_NO_DEADLINE_MS;
    if (m_impl->more_pending) return 0;
    if (m_impl->endpoint != nullptr) return WL_POLL_NO_DEADLINE_MS;
    const auto count = m_impl->config.poll_interval.count();
    return static_cast<std::uint32_t>(std::min<std::int64_t>(count,
        std::numeric_limits<std::uint32_t>::max() - 1U));
}

std::uint16_t UdpAdapter::local_port() const
{
    std::error_code error;
    const auto endpoint = m_impl->socket.local_endpoint(error);
    return error ? 0 : endpoint.port();
}

void UdpAdapter::get_stats(UdpAdapterStats& out_stats) const { out_stats = m_impl->stats; }

void UdpAdapter::get_common_stats(wl_adapter_stats_t& out_stats) const noexcept
{
    out_stats = wl_adapter_stats_t{
        .rx_units = m_impl->stats.rx_datagrams,
        .rx_bytes = m_impl->stats.rx_bytes,
        .rx_backpressure = m_impl->stats.rx_pauses,
        .tx_units = m_impl->stats.tx_datagrams,
        .tx_bytes = m_impl->stats.tx_bytes,
        .tx_completions = m_impl->stats.tx_datagrams,
        .activity_notifications = m_impl->stats.activity_notifications,
        .service_calls = m_impl->stats.service_calls,
        .errors = m_impl->stats.errors,
        .started = static_cast<std::uint8_t>(!m_impl->quiesced),
        .rx_paused = 0,
        .tx_active = 0,
    };
}
} // namespace wirelink::asio

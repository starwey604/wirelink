/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/asio/udp_adapter.hpp"

#include <asio.hpp>

#include <limits>
#include <utility>

namespace wirelink::asio
{
class UdpAdapter::Impl
{
public:
    Impl(wl_ctx_t& context, std::size_t maximum,
         std::chrono::milliseconds interval)
        : link(context), socket(io), maximum_datagram_size(maximum),
          poll_interval(interval)
    {
    }

    wl_ctx_t& link;
    ::asio::io_context io;
    ::asio::ip::udp::socket socket;
    ::asio::ip::udp::endpoint peer;
    bool have_peer{};
    std::size_t maximum_datagram_size{};
    std::chrono::milliseconds poll_interval{1};
    bool quiesced{};
    UdpAdapterStats stats{};
};

UdpAdapter::UdpAdapter(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

UdpAdapter::~UdpAdapter()
{
    if (!m_impl) return;
    quiesce();
}

std::unique_ptr<UdpAdapter> UdpAdapter::open(wl_ctx_t& link,
                                             const UdpAdapterConfig& config,
                                             std::error_code& error)
{
    wl_config_t link_config{};
    error.clear();
    if (config.maximum_datagram_size == 0 || config.poll_interval.count() <= 0 ||
        wl_get_config(&link, &link_config) != WL_OK ||
        link_config.envelope != WL_ENVELOPE_COBS_STREAM ||
        link_config.integrity != WL_INTEGRITY_NONE)
    {
        error = std::make_error_code(std::errc::invalid_argument);
        return nullptr;
    }

    const auto address = ::asio::ip::make_address(config.bind_address, error);
    if (error) return nullptr;
    auto impl = std::make_unique<Impl>(link, config.maximum_datagram_size,
                                      config.poll_interval);
    impl->socket.open(address.is_v6() ? ::asio::ip::udp::v6() :
                                       ::asio::ip::udp::v4(), error);
    if (error) return nullptr;
    impl->socket.set_option(::asio::socket_base::reuse_address(true), error);
    if (error) return nullptr;
    impl->socket.bind({address, config.bind_port}, error);
    if (error) return nullptr;
    impl->socket.non_blocking(true, error);
    if (error) return nullptr;

    auto adapter = std::unique_ptr<UdpAdapter>(new UdpAdapter(std::move(impl)));
    if (wl_set_sink(&link, sink, adapter.get()) != WL_OK)
    {
        error = std::make_error_code(std::errc::invalid_argument);
        return nullptr;
    }
    return adapter;
}

int UdpAdapter::set_peer(std::string_view address, std::uint16_t port)
{
    if (port == 0) return WL_ERR_INVALID_ARG;
    std::error_code error;
    const auto parsed = ::asio::ip::make_address(std::string(address), error);
    if (error) return WL_ERR_INVALID_ARG;
    m_impl->peer = {parsed, port};
    m_impl->have_peer = true;
    return WL_OK;
}

wl_sink_result_t UdpAdapter::sink(void* user_data, wl_io_token_t token,
                                  const std::uint8_t* data,
                                  std::size_t length)
{
    auto* adapter = static_cast<UdpAdapter*>(user_data);
    (void)token;
    if (adapter == nullptr || data == nullptr || length == 0)
        return WL_SINK_FAILED;
    auto& impl = *adapter->m_impl;
    if (!impl.have_peer || length > impl.maximum_datagram_size)
        return WL_SINK_FAILED;

    std::error_code error;
    const auto sent = impl.socket.send_to(::asio::buffer(data, length),
                                          impl.peer, 0, error);
    if (error == ::asio::error::would_block || error == ::asio::error::try_again)
        return WL_SINK_BUSY;
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
    if (!m_impl || m_impl->quiesced) return WL_ERR_INVALID_STATE;
    wl_rx_dma_claim_t claim{};
    int result = wl_rx_dma_claim(&m_impl->link,
                                 m_impl->maximum_datagram_size, &claim);
    if (result != WL_OK)
    {
        ++m_impl->stats.rx_pauses;
        return result;
    }
    if (claim.span.length < m_impl->maximum_datagram_size)
    {
        (void)wl_rx_dma_finish(&m_impl->link, &claim);
        ++m_impl->stats.rx_pauses;
        return WL_ERR_WOULD_BLOCK;
    }

    ::asio::ip::udp::endpoint source;
    std::error_code error;
    const auto received = m_impl->socket.receive_from(
        ::asio::buffer(claim.span.data, claim.span.length), source, 0, error);
    if (error == ::asio::error::would_block || error == ::asio::error::try_again)
    {
        (void)wl_rx_dma_finish(&m_impl->link, &claim);
        return WL_ERR_NO_DATA;
    }
    if (error)
    {
        (void)wl_rx_dma_finish(&m_impl->link, &claim);
        ++m_impl->stats.errors;
        return WL_ERR_IO;
    }
    if (m_impl->have_peer && source != m_impl->peer)
    {
        (void)wl_rx_dma_finish(&m_impl->link, &claim);
        ++m_impl->stats.rx_rejected;
        return WL_ERR_NOT_FOUND;
    }
    if (received == 0 ||
        wl_rx_dma_publish(&m_impl->link, &claim, 0, received) != WL_OK ||
        wl_rx_dma_finish(&m_impl->link, &claim) != WL_OK)
    {
        (void)wl_rx_dma_abort(&m_impl->link);
        ++m_impl->stats.errors;
        return WL_ERR_IO;
    }
    ++m_impl->stats.rx_datagrams;
    m_impl->stats.rx_bytes += received;
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

std::uint32_t UdpAdapter::deadline_hint(wl_time_ms_t now_ms) const noexcept
{
    (void)now_ms;
    if (!m_impl || m_impl->quiesced) return WL_POLL_NO_DEADLINE_MS;
    const std::int64_t count = m_impl->poll_interval.count();
    if (count >= static_cast<std::int64_t>(
                     std::numeric_limits<std::uint32_t>::max()))
        return std::numeric_limits<std::uint32_t>::max() - 1U;
    return static_cast<std::uint32_t>(count);
}

std::uint16_t UdpAdapter::local_port() const
{
    std::error_code error;
    const auto endpoint = m_impl->socket.local_endpoint(error);
    return error ? 0 : endpoint.port();
}

void UdpAdapter::get_stats(UdpAdapterStats& out_stats) const
{
    out_stats = m_impl->stats;
}
} // namespace wirelink::asio

/* SPDX-License-Identifier: Apache-2.0 */

#ifndef WIRELINK_ASIO_UDP_ADAPTER_HPP_
#define WIRELINK_ASIO_UDP_ADAPTER_HPP_

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include "wirelink/wirelink.h"

namespace wirelink::asio
{
struct UdpAdapterConfig
{
    std::string bind_address{"0.0.0.0"};
    std::uint16_t bind_port{};
    std::size_t maximum_datagram_size{1200};
    // The synchronous non-blocking socket has no readiness callback. This
    // bounded owner wake keeps receive latency explicit instead of busy-spin.
    std::chrono::milliseconds poll_interval{1};
    // If true, the first accepted source becomes the fixed peer. Intended for
    // devices that announce themselves before the host can address them.
    bool learn_peer_from_first_datagram{};
};

struct UdpAdapterStats
{
    std::uint64_t rx_datagrams{};
    std::uint64_t rx_bytes{};
    std::uint64_t rx_rejected{};
    std::uint64_t rx_pauses{};
    std::uint64_t peer_learns{};
    std::uint64_t tx_datagrams{};
    std::uint64_t tx_bytes{};
    std::uint64_t errors{};
};

class UdpAdapter
{
public:
    static std::unique_ptr<UdpAdapter> open(wl_ctx_t& link,
                                            const UdpAdapterConfig& config,
                                            std::error_code& error);
    ~UdpAdapter();

    UdpAdapter(const UdpAdapter&) = delete;
    UdpAdapter& operator=(const UdpAdapter&) = delete;

    int set_peer(std::string_view address, std::uint16_t port);
    int service();
    void quiesce() noexcept;
    [[nodiscard]] std::uint32_t deadline_hint(wl_time_ms_t now_ms) const noexcept;
    [[nodiscard]] std::uint16_t local_port() const;
    void get_stats(UdpAdapterStats& out_stats) const;

private:
    class Impl;
    explicit UdpAdapter(std::unique_ptr<Impl> impl);
    static wl_sink_result_t sink(void* user_data, wl_io_token_t token,
                                 const std::uint8_t* data, std::size_t length);

    std::unique_ptr<Impl> m_impl;
};
} // namespace wirelink::asio

#endif // WIRELINK_ASIO_UDP_ADAPTER_HPP_

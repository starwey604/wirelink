/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/asio/udp_adapter.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <system_error>
#include <thread>

namespace
{
struct Endpoint
{
    wl_ctx_t context{};
    std::array<std::uint8_t, 64> tx_payload{};
    std::array<std::uint8_t, 128> tx_unit{};
    std::array<std::uint8_t, 128> control_unit{};
    std::array<std::uint8_t, 256> rx_fifo{};
    std::array<std::uint8_t, 128> rx_fallback{};

    explicit Endpoint(std::uint64_t session)
    {
        const wl_config_t config{
            .max_payload_len = 64,
            .envelope = WL_ENVELOPE_COBS_STREAM,
            .integrity = WL_INTEGRITY_NONE,
            .session_id = session,
            .max_retries = 0,
            .ack_timeout_ms = 20,
            .max_transmission_unit = 128,
        };
        const wl_storage_t storage{
            .tx_payload = tx_payload.data(),
            .tx_payload_size = tx_payload.size(),
            .tx_unit = tx_unit.data(),
            .tx_unit_size = tx_unit.size(),
            .control_unit = control_unit.data(),
            .control_unit_size = control_unit.size(),
            .rx_fifo = rx_fifo.data(),
            .rx_fifo_size = rx_fifo.size(),
            .rx_fallback = rx_fallback.data(),
            .rx_fallback_size = rx_fallback.size(),
        };
        assert(wl_init(&context, &config, &storage) == WL_OK);
    }
};
} // namespace

int main()
{
    Endpoint left(UINT64_C(0x5544504c45465431));
    Endpoint right(UINT64_C(0x5544505249474854));
    const wirelink::asio::UdpAdapterConfig config{
        .bind_address = "127.0.0.1",
        .bind_port = 0,
        .maximum_datagram_size = 128,
    };
    const wirelink::asio::UdpAdapterConfig learning_config{
        .bind_address = "127.0.0.1",
        .bind_port = 0,
        .maximum_datagram_size = 128,
        .learn_peer_from_first_datagram = true,
    };
    std::error_code error;
    auto left_udp = wirelink::asio::UdpAdapter::open(left.context, config, error);
    assert(left_udp && !error);
    auto right_udp = wirelink::asio::UdpAdapter::open(right.context,
                                                       learning_config, error);
    assert(right_udp && !error);
    assert(left_udp->set_peer("127.0.0.1", right_udp->local_port()) == WL_OK);
    assert(right_udp->deadline_hint(0) == 1);

    constexpr std::array<std::uint8_t, 5> payload{1, 3, 5, 7, 9};
    assert(wl_send_unreliable(&left.context, 0x42, payload.data(),
                              payload.size()) == WL_OK);

    wl_event_t event{};
    bool received = false;
    for (unsigned int attempt = 0; attempt < 100 && !received; ++attempt)
    {
        const int service_result = right_udp->service();
        assert(service_result == WL_OK || service_result == WL_ERR_NO_DATA ||
               service_result == WL_ERR_WOULD_BLOCK);
        if (wl_poll(&right.context, attempt, &event) == WL_OK)
            received = event.type == WL_EVT_UNRELIABLE_RX;
        if (!received) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(received);
    assert(event.message_id == 0x42);
    assert(event.payload_len == payload.size());
    for (std::size_t i = 0; i < payload.size(); ++i)
        assert(event.payload[i] == payload[i]);
    wl_event_release(&right.context, &event);

    wirelink::asio::UdpAdapterStats stats{};
    right_udp->get_stats(stats);
    assert(stats.rx_datagrams == 1);
    assert(stats.rx_bytes > payload.size());
    assert(stats.peer_learns == 1);

    constexpr std::array<std::uint8_t, 2> reply{4, 2};
    assert(wl_send_unreliable(&right.context, 0x43, reply.data(),
                              reply.size()) == WL_OK);
    received = false;
    for (unsigned int attempt = 0; attempt < 100 && !received; ++attempt)
    {
        const int service_result = left_udp->service();
        assert(service_result == WL_OK || service_result == WL_ERR_NO_DATA ||
               service_result == WL_ERR_WOULD_BLOCK);
        if (wl_poll(&left.context, attempt, &event) == WL_OK)
            received = event.type == WL_EVT_UNRELIABLE_RX;
        if (!received) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(received && event.message_id == 0x43);
    wl_event_release(&left.context, &event);
    right_udp->quiesce();
    assert(right_udp->deadline_hint(0) == WL_POLL_NO_DEADLINE_MS);
    assert(right_udp->service() == WL_ERR_INVALID_STATE);
    return 0;
}

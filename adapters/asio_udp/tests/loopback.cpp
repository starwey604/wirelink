/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/asio/udp_adapter.hpp"
#include "wirelink/frame.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <asio.hpp>
#include <vector>
#include <chrono>
#include <cstdint>
#include <system_error>
#include <thread>

#define CHECK(expression) do { if (!(expression)) { \
    std::cerr << "line " << __LINE__ << ": " << #expression << "\n"; \
    std::abort(); } } while (false)

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

    explicit Endpoint(std::uint64_t session, wl_integrity_t integrity)
    {
        const wl_config_t config{
            .max_payload_len = 64,
            .envelope = WL_ENVELOPE_COBS_STREAM,
            .integrity = integrity,
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
        CHECK(wl_init(&context, &config, &storage) == WL_OK);
    }
};
} // namespace

void legacy_stream(wl_integrity_t integrity)
{
    Endpoint left(UINT64_C(0x5544504c45465431), integrity);
    Endpoint right(UINT64_C(0x5544505249474854), integrity);
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
    CHECK(left_udp && !error);
    auto right_udp = wirelink::asio::UdpAdapter::open(right.context,
                                                       learning_config, error);
    CHECK(right_udp && !error);
    CHECK(left_udp->set_peer("127.0.0.1", right_udp->local_port()) == WL_OK);
    CHECK(right_udp->deadline_hint(0) == 1);

    constexpr std::array<std::uint8_t, 5> payload{1, 3, 5, 7, 9};
    CHECK(wl_send_unreliable(&left.context, 0x42, payload.data(),
                              payload.size()) == WL_OK);

    wl_event_t event{};
    bool received = false;
    for (unsigned int attempt = 0; attempt < 100 && !received; ++attempt)
    {
        const int service_result = right_udp->service();
        CHECK(service_result == WL_OK || service_result == WL_ERR_NO_DATA ||
               service_result == WL_ERR_WOULD_BLOCK);
        if (wl_poll(&right.context, attempt, &event) == WL_OK)
            received = event.type == WL_EVT_UNRELIABLE_RX;
        if (!received) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(received);
    CHECK(event.message_id == 0x42);
    CHECK(event.payload_len == payload.size());
    for (std::size_t i = 0; i < payload.size(); ++i)
        CHECK(event.payload[i] == payload[i]);
    wl_event_release(&right.context, &event);

    wirelink::asio::UdpAdapterStats stats{};
    right_udp->get_stats(stats);
    CHECK(stats.rx_datagrams == 1);
    CHECK(stats.rx_bytes > payload.size());
    CHECK(stats.peer_learns == 1);
    wl_adapter_stats_t common{};
    right_udp->get_common_stats(common);
    CHECK(common.rx_units == 1 && common.rx_bytes == stats.rx_bytes);
    CHECK(common.service_calls > 0 && common.errors == 0 && common.started == 1);

    constexpr std::array<std::uint8_t, 2> reply{4, 2};
    CHECK(wl_send_unreliable(&right.context, 0x43, reply.data(),
                              reply.size()) == WL_OK);
    received = false;
    for (unsigned int attempt = 0; attempt < 100 && !received; ++attempt)
    {
        const int service_result = left_udp->service();
        CHECK(service_result == WL_OK || service_result == WL_ERR_NO_DATA ||
               service_result == WL_ERR_WOULD_BLOCK);
        if (wl_poll(&left.context, attempt, &event) == WL_OK)
            received = event.type == WL_EVT_UNRELIABLE_RX;
        if (!received) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(received && event.message_id == 0x43);
    wl_event_release(&left.context, &event);
    right_udp->quiesce();
    right_udp->get_common_stats(common);
    CHECK(common.started == 0);
    CHECK(right_udp->deadline_hint(0) == WL_POLL_NO_DEADLINE_MS);
    CHECK(right_udp->service() == WL_ERR_INVALID_STATE);
}


namespace
{
struct NativeEndpoint
{
    wl_endpoint_t endpoint{};
    std::array<std::uint8_t, 64> payload{};
    std::array<std::uint8_t, 128> tx{}, control{}, fallback{};
    unsigned received{};
    std::uint8_t value{};
    wl_integrity_t integrity;

    void init()
    {
        const wl_config_t config{
            .max_payload_len = 64, .envelope = WL_ENVELOPE_NATIVE_PACKET,
            .integrity = integrity, .session_id = 7,
            .max_retries = 3, .ack_timeout_ms = 20};
        const wl_storage_t storage{
            .tx_payload = payload.data(), .tx_payload_size = payload.size(),
            .tx_unit = tx.data(), .tx_unit_size = tx.size(),
            .control_unit = control.data(), .control_unit_size = control.size(),
            .rx_fallback = fallback.data(), .rx_fallback_size = fallback.size()};
        wl_pump_hooks_t hooks{};
        hooks.application_user_data = this;
        hooks.on_event = [](void* context, wl_ctx_t*, const wl_event_t* event, wl_time_ms_t) {
            auto& self = *static_cast<NativeEndpoint*>(context);
            if (event->type == WL_EVT_UNRELIABLE_RX) {
                ++self.received;
                self.value = event->payload[0];
            }
            return WL_PUMP_EVENT_UNHANDLED;
        };
        CHECK(wl_endpoint_init(&endpoint, &config, &storage, &hooks) == WL_OK);
    }
    explicit NativeEndpoint(wl_integrity_t mode = WL_INTEGRITY_CRC32C) : integrity(mode) { init(); }
    ~NativeEndpoint() { wl_endpoint_close(&endpoint); }
};
}

void native_packets(wl_integrity_t integrity)
{
    NativeEndpoint left(integrity), right(integrity);
    wirelink::asio::UdpAdapterConfig config;
    config.bind_address = "127.0.0.1";
    config.receive_slots = 2;
    config.service_budget = 2;
    std::error_code error;
    auto a = wirelink::asio::UdpAdapter::open(left.endpoint, config, error);
    CHECK(a && !error);
    auto b = wirelink::asio::UdpAdapter::open(right.endpoint, config, error);
    CHECK(b && !error);
    CHECK(wl_endpoint_has_adapter(&left.endpoint));
    CHECK(a->set_peer("127.0.0.1", b->local_port()) == WL_OK);
    CHECK(b->set_peer("127.0.0.1", a->local_port()) == WL_OK);
    CHECK(a->set_peer("127.0.0.1", 1) == WL_ERR_INVALID_STATE);
    CHECK(!wirelink::asio::UdpAdapter::open(left.endpoint, config, error));
    CHECK(a->deadline_hint(0) == WL_POLL_NO_DEADLINE_MS);
    CHECK(b->wait_for_activity(std::chrono::milliseconds(10)) == WL_ERR_NO_DATA);
    CHECK(b->wait_for_activity(std::chrono::milliseconds(-1)) == WL_ERR_INVALID_ARG);

    for (std::uint8_t n = 1; n <= 5; ++n)
        CHECK(wl_send_unreliable(wl_endpoint_link(&left.endpoint), 1, &n, 1) == WL_OK);
    CHECK(b->wait_for_activity(std::chrono::milliseconds(100)) == WL_OK);
    // A bounded service can only publish two datagrams before owner dispatch.
    CHECK(b->service() == WL_OK);
    wirelink::asio::UdpAdapterStats stats{};
    b->get_stats(stats);
    CHECK(stats.rx_datagrams == 2);
    CHECK(b->service() == WL_ERR_WOULD_BLOCK);
    for (unsigned attempt = 0; attempt < 20 && right.received < 5; ++attempt) {
        CHECK(wl_endpoint_step(&right.endpoint, attempt, 16) == WL_OK);
        if (right.received < 5) {
            const int waited = b->wait_for_activity(std::chrono::milliseconds(10));
            CHECK(waited == WL_OK || waited == WL_ERR_NO_DATA);
        }
    }
    CHECK(right.received == 5 && right.value == 5);
    CHECK(wl_endpoint_step(&right.endpoint, 30, 16) == WL_OK);
    b->get_stats(stats);
    CHECK(stats.activity_notifications >= 1 && stats.wait_timeouts == 1);
    CHECK(stats.rx_pauses >= 1);

    // A wrong source, even with a valid frame, must not enter the endpoint.
    ::asio::io_context io;
    ::asio::ip::udp::socket stranger(io, {::asio::ip::udp::v4(), 0});
    const auto destination = ::asio::ip::udp::endpoint(::asio::ip::make_address("127.0.0.1"), b->local_port());
    std::array<std::uint8_t, 128> valid_frame{};
    std::size_t valid_size{};
    const std::uint8_t forbidden = 99;
    const wl_wire_packet_t packet{.type = WL_PACKET_DATA, .integrity = integrity,
        .flags = 0, .message_id = 1, .session_id = 0, .sequence = 0,
        .payload = &forbidden, .payload_len = 1};
    CHECK(wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, valid_frame.data(),
                         valid_frame.size(), &valid_size) == WL_OK);
    stranger.send_to(::asio::buffer(valid_frame.data(), valid_size), destination);
    CHECK(b->wait_for_activity(std::chrono::milliseconds(100)) == WL_OK);
    CHECK(wl_endpoint_step(&right.endpoint, 31, 16) == WL_OK);
    b->get_stats(stats);
    CHECK(stats.rx_rejected == 1 && right.received == 5);

    // Closing the endpoint, then reinitializing it, cannot let the old
    // adapter's destructor close the new incarnation.
    wl_endpoint_close(&right.endpoint);
    CHECK(b->service() == WL_ERR_INVALID_STATE);
    right.init();
    b.reset();
    CHECK(wl_endpoint_link(&right.endpoint) != nullptr);
    a.reset();
    CHECK(wl_endpoint_link(&left.endpoint) == nullptr);
}

void invalid_datagrams()
{
    NativeEndpoint endpoint;
    wirelink::asio::UdpAdapterConfig config;
    config.bind_address = "127.0.0.1";
    config.maximum_datagram_size = 128;
    std::error_code error;
    auto adapter = wirelink::asio::UdpAdapter::open(endpoint.endpoint, config, error);
    CHECK(adapter && !error);
    ::asio::io_context io;
    ::asio::ip::udp::socket sender(io, {::asio::ip::udp::v4(), 0});
    CHECK(adapter->set_peer("127.0.0.1", sender.local_endpoint().port()) == WL_OK);
    const auto target = ::asio::ip::udp::endpoint(::asio::ip::make_address("127.0.0.1"), adapter->local_port());
    const std::vector<std::uint8_t> huge(1024, 0x55);
    sender.send_to(::asio::buffer(huge), target);
    sender.send_to(::asio::buffer(huge.data(), 129), target);
    sender.send_to(::asio::buffer(huge.data(), 0), target);
    CHECK(adapter->wait_for_activity(std::chrono::milliseconds(100)) == WL_OK);
    CHECK(wl_endpoint_step(&endpoint.endpoint, 1, 16) == WL_OK);
    wirelink::asio::UdpAdapterStats stats{};
    adapter->get_stats(stats);
    CHECK(stats.rx_rejected == 3 && stats.rx_datagrams == 0);
    CHECK(endpoint.received == 0);
    // Neither an oversized nor an empty packet may be published as a frame.
    CHECK(wl_endpoint_step(&endpoint.endpoint, 2, 16) == WL_OK);

    NativeEndpoint invalid;
    config.maximum_datagram_size = 1;
    CHECK(!wirelink::asio::UdpAdapter::open(invalid.endpoint, config, error));
    config.maximum_datagram_size = 65508;
    CHECK(!wirelink::asio::UdpAdapter::open(invalid.endpoint, config, error));
    config.maximum_datagram_size = 128;
    config.receive_slots = 1;
    CHECK(!wirelink::asio::UdpAdapter::open(invalid.endpoint, config, error));
    CHECK(wl_endpoint_has_adapter(&invalid.endpoint) == 0);
}

int main()
{
    for (const auto integrity : {WL_INTEGRITY_NONE, WL_INTEGRITY_CRC32C}) {
        legacy_stream(integrity);
        native_packets(integrity);
    }
    invalid_datagrams();
    std::cout << "UDP: legacy stream, native queue, readiness, peer filtering, bounds, lifetime OK\n";
}

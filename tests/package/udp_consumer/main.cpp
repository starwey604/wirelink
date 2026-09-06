/* SPDX-License-Identifier: Apache-2.0 */
#include <array>
#include <chrono>
#include "wirelink/asio/udp_adapter.hpp"

int main()
{
    wl_endpoint_t endpoint{};
    std::array<std::uint8_t, 128> payload{}, tx{}, control{}, rx{};
    const wl_config_t config{.max_payload_len = 32,
        .envelope = WL_ENVELOPE_NATIVE_PACKET, .integrity = WL_INTEGRITY_CRC32C,
        .session_id = 1, .max_retries = 0, .ack_timeout_ms = 0,
        .max_transmission_unit = 0};
    const wl_storage_t storage{.tx_payload = payload.data(), .tx_payload_size = payload.size(),
        .tx_unit = tx.data(), .tx_unit_size = tx.size(),
        .control_unit = control.data(), .control_unit_size = control.size(),
        .rx_fifo = nullptr, .rx_fifo_size = 0,
        .rx_fallback = rx.data(), .rx_fallback_size = rx.size()};
    if (wl_endpoint_init(&endpoint, &config, &storage, nullptr) != WL_OK) return 1;
    wirelink::asio::UdpAdapterConfig udp;
    udp.bind_address = "127.0.0.1";
    std::error_code error;
    auto adapter = wirelink::asio::UdpAdapter::open(endpoint, udp, error);
    if (!adapter || error) return 2;
    if (adapter->wait_for_activity(std::chrono::milliseconds(1)) != WL_ERR_NO_DATA) return 3;
    if (wl_endpoint_step(&endpoint, 0, 8) != WL_OK) return 4;
    adapter.reset();
    return wl_endpoint_link(&endpoint) == nullptr ? 0 : 5;
}

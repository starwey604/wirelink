/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/astrial/serial_adapter.hpp"
#include "control.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <poll.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace
{
using namespace std::chrono_literals;

void count_activity(void* user_data) noexcept
{
    static_cast<std::atomic<std::uint64_t>*>(user_data)->fetch_add(
        1, std::memory_order_relaxed);
}

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

class PseudoTerminal
{
public:
    PseudoTerminal()
    {
        int slave = -1;
        std::array<char, 256> name{};
        if (::openpty(&m_master, &slave, name.data(), nullptr, nullptr) != 0)
        {
            throw std::system_error(errno, std::generic_category(), "openpty");
        }
        termios attributes{};
        if (::tcgetattr(slave, &attributes) != 0)
        {
            const int saved_errno = errno;
            ::close(slave);
            throw std::system_error(saved_errno, std::generic_category(), "tcgetattr");
        }
        ::cfmakeraw(&attributes);
        if (::tcsetattr(slave, TCSANOW, &attributes) != 0)
        {
            const int saved_errno = errno;
            ::close(slave);
            throw std::system_error(saved_errno, std::generic_category(), "tcsetattr");
        }
        m_slave_name = name.data();
        ::close(slave);
    }

    ~PseudoTerminal()
    {
        if (m_master >= 0) ::close(m_master);
    }

    [[nodiscard]] const std::string& slave_name() const { return m_slave_name; }

    void write_all(std::span<const uint8_t> bytes) const
    {
        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            const auto written = ::write(m_master, bytes.data() + offset, bytes.size() - offset);
            if (written < 0)
            {
                if (errno == EINTR) continue;
                throw std::system_error(errno, std::generic_category(), "PTY write");
            }
            offset += static_cast<std::size_t>(written);
        }
    }

    std::vector<uint8_t> read_cobs_unit() const
    {
        std::vector<uint8_t> result;
        while (result.empty() || result.back() != 0)
        {
            pollfd descriptor{m_master, POLLIN, 0};
            const int ready = ::poll(&descriptor, 1, 2000);
            if (ready == 0) throw std::runtime_error("timed out reading PTY master");
            if (ready < 0)
            {
                if (errno == EINTR) continue;
                throw std::system_error(errno, std::generic_category(), "PTY poll");
            }
            uint8_t byte{};
            const auto received = ::read(m_master, &byte, 1);
            if (received < 0)
            {
                if (errno == EINTR) continue;
                throw std::system_error(errno, std::generic_category(), "PTY read");
            }
            if (received == 1) result.push_back(byte);
        }
        return result;
    }

private:
    int m_master{-1};
    std::string m_slave_name;
};

struct Endpoint
{
    wl_ctx_t context{};
    wl_config_t config{};
    wl_storage_t storage{};
    std::array<uint8_t, 256> tx_payload{};
    std::array<uint8_t, WL_FRAME_MAX_COBS_LEN> tx_unit{};
    std::array<uint8_t, WL_FRAME_MAX_COBS_LEN> control_unit{};
    std::array<uint8_t, WL_FRAME_MAX_COBS_LEN> rx_fifo{};
    std::array<uint8_t, WL_FRAME_MAX_COBS_LEN> rx_fallback{};
    std::vector<uint8_t> outbound;

    explicit Endpoint(uint64_t session_id,
                      std::size_t rx_capacity = WL_FRAME_MAX_COBS_LEN,
                      uint16_t max_payload_length = 64)
    {
        config.max_payload_len = max_payload_length;
        config.envelope = WL_ENVELOPE_COBS_STREAM;
        config.integrity = WL_INTEGRITY_CRC32C;
        config.session_id = session_id;
        config.max_retries = 1;
        config.ack_timeout_ms = 20;
        config.max_transmission_unit = tx_unit.size();

        storage.tx_payload = tx_payload.data();
        storage.tx_payload_size = tx_payload.size();
        storage.tx_unit = tx_unit.data();
        storage.tx_unit_size = tx_unit.size();
        storage.control_unit = control_unit.data();
        storage.control_unit_size = control_unit.size();
        storage.rx_fifo = rx_fifo.data();
        storage.rx_fifo_size = rx_capacity;
        storage.rx_fallback = rx_fallback.data();
        storage.rx_fallback_size = rx_fallback.size();
        require(wl_init(&context, &config, &storage) == WL_OK, "wl_init failed");
    }

    static wl_sink_result_t capture(void* user_data, wl_io_token_t,
                                    const uint8_t* data, size_t length)
    {
        auto& endpoint = *static_cast<Endpoint*>(user_data);
        if (!endpoint.outbound.empty()) return WL_SINK_BUSY;
        endpoint.outbound.assign(data, data + length);
        return WL_SINK_SENT;
    }

    void enable_capture()
    {
        require(wl_set_sink(&context, capture, this) == WL_OK, "wl_set_sink failed");
    }
};

bool poll_until_event(wirelink::astrial::SerialAdapter& adapter, Endpoint& endpoint,
                      wl_event_t& event, wl_event_type_t wanted)
{
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        const int polled = wl_poll(&endpoint.context, 1, &event);
        require(polled == WL_OK || polled == WL_ERR_NO_DATA, "wl_poll failed");
        require(adapter.service() == WL_OK, "adapter service failed");
        if (polled == WL_OK && event.type == wanted) return true;
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

bool points_into(const void* pointer, std::size_t length,
                 const uint8_t* storage, std::size_t storage_length)
{
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const auto begin = reinterpret_cast<std::uintptr_t>(storage);
    return address >= begin && length <= storage_length &&
           address - begin <= storage_length - length;
}

void test_wirelink_round_trip()
{
    PseudoTerminal terminal;
    Endpoint device(0x1111);
    Endpoint peer(0x2222);
    std::atomic<std::uint64_t> activity_count{};
    peer.enable_capture();

    wirelink::astrial::SerialConfig adapter_config;
    adapter_config.port = terminal.slave_name();
    adapter_config.baud_rate = 115200;
    adapter_config.auto_reconnect = false;
    adapter_config.activity_callback = count_activity;
    adapter_config.activity_user_data = &activity_count;
    auto opened = wirelink::astrial::SerialAdapter::open(device.context, adapter_config);
    if (!opened) throw std::system_error(opened.error(), "open Wirelink Astrial adapter");
    auto adapter = std::move(opened.value());

    static constexpr std::array<uint8_t, 5> inbound_payload{1, 0, 2, 0, 3};
    require(wl_send_unreliable(&peer.context, 0x42, inbound_payload.data(),
                               inbound_payload.size()) == WL_OK,
            "peer encode failed");
    wl_event_t event{};
    require(wl_poll(&peer.context, 0, &event) == WL_OK &&
                event.type == WL_EVT_TX_SUCCESS,
            "peer did not report its local TX completion");
    terminal.write_all(peer.outbound);
    peer.outbound.clear();

    event = {};
    require(poll_until_event(*adapter, device, event, WL_EVT_UNRELIABLE_RX),
            "adapter did not deliver inbound frame");
    require(event.message_id == 0x42, "inbound command id mismatch");
    require(event.payload_len == inbound_payload.size(), "inbound payload length mismatch");
    require(std::memcmp(event.payload, inbound_payload.data(), inbound_payload.size()) == 0,
            "inbound payload mismatch");
    wl_event_release(&device.context, &event);

    static constexpr std::array<uint8_t, 4> outbound_payload{9, 8, 0, 7};
    require(wl_send_unreliable(&device.context, 0x84, outbound_payload.data(),
                               outbound_payload.size()) == WL_OK,
            "adapter TX submission failed");
    const auto encoded = terminal.read_cobs_unit();

    event = {};
    require(poll_until_event(*adapter, device, event, WL_EVT_TX_SUCCESS),
            "deferred TX completion was not delivered");

    size_t accepted = 0;
    require(wl_feed_bytes(&peer.context, encoded.data(), encoded.size(), &accepted) == WL_OK,
            "peer did not accept adapter TX bytes");
    require(accepted == encoded.size(), "peer accepted a partial adapter frame");
    event = {};
    require(wl_poll(&peer.context, 2, &event) == WL_OK, "peer did not decode adapter TX frame");
    require(event.type == WL_EVT_UNRELIABLE_RX, "peer received wrong event type");
    require(event.message_id == 0x84, "outbound command id mismatch");
    require(event.payload_len == outbound_payload.size(), "outbound payload length mismatch");
    require(std::memcmp(event.payload, outbound_payload.data(), outbound_payload.size()) == 0,
            "outbound payload mismatch");
    wl_event_release(&peer.context, &event);

    wirelink::astrial::SerialAdapterStats stats;
    adapter->get_stats(stats);
    require(stats.rx_bytes >= inbound_payload.size(),
            "RX byte statistics were not updated");
    require(stats.rx_reservations > 0, "RX did not borrow Wirelink ring storage");
    require(stats.tx_submissions == 1, "TX submission statistics mismatch");
    require(stats.tx_completions == 1, "TX completion statistics mismatch");
    require(stats.errors == 0, "adapter reported unexpected errors");
    require(activity_count.load(std::memory_order_relaxed) >= 2,
            "RX/TX completions did not notify the owner");
    require(adapter->deadline_hint(0) == WL_POLL_NO_DEADLINE_MS,
            "event-driven serial adapter exposed a polling deadline");
    adapter->quiesce();
    adapter->get_stats(stats);
    require(!stats.started, "serial adapter did not quiesce");
}

void test_rx_backpressure_resume()
{
    PseudoTerminal terminal;
    Endpoint device(0x3333, 128);
    Endpoint peer(0x4444);
    peer.enable_capture();

    wirelink::astrial::SerialConfig adapter_config;
    adapter_config.port = terminal.slave_name();
    adapter_config.auto_reconnect = false;
    auto opened = wirelink::astrial::SerialAdapter::open(device.context, adapter_config);
    if (!opened) throw std::system_error(opened.error(), "open backpressure adapter");
    auto adapter = std::move(opened.value());

    std::vector<uint8_t> stream;
    static constexpr std::size_t frame_count = 20;
    for (std::size_t i = 0; i < frame_count; ++i)
    {
        const uint8_t payload = static_cast<uint8_t>(i);
        require(wl_send_unreliable(&peer.context, 0x55, &payload, 1) == WL_OK,
                "backpressure peer encode failed");
        stream.insert(stream.end(), peer.outbound.begin(), peer.outbound.end());
        peer.outbound.clear();

        wl_event_t tx_event{};
        require(wl_poll(&peer.context, 0, &tx_event) == WL_OK &&
                    tx_event.type == WL_EVT_TX_SUCCESS,
                "backpressure peer TX event missing");
    }
    terminal.write_all(stream);

    std::size_t received = 0;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (received < frame_count && std::chrono::steady_clock::now() < deadline)
    {
        wl_event_t event{};
        const int polled = wl_poll(&device.context, 1, &event);
        require(polled == WL_OK || polled == WL_ERR_NO_DATA,
                "backpressure wl_poll failed");
        if (polled == WL_OK && event.type == WL_EVT_UNRELIABLE_RX)
        {
            require(event.message_id == 0x55 && event.payload_len == 1,
                    "backpressure frame metadata mismatch");
            require(event.payload[0] == static_cast<uint8_t>(received),
                    "backpressure frame order mismatch");
            ++received;
            wl_event_release(&device.context, &event);
        }
        require(adapter->service() == WL_OK, "backpressure service failed");
        std::this_thread::sleep_for(1ms);
    }
    require(received == frame_count, "backpressure lost a frame");

    wirelink::astrial::SerialAdapterStats stats;
    adapter->get_stats(stats);
    require(stats.rx_pauses > 0, "finite RX ring never applied backpressure");
    require(stats.rx_bytes == stream.size(), "backpressure RX byte count mismatch");
    require(stats.errors == 0, "backpressure path reported an error");
}

void test_generated_codec_borrows_event_payload()
{
    PseudoTerminal terminal;
    Endpoint device(0x5555, WL_FRAME_MAX_COBS_LEN, 256);
    Endpoint peer(0x6666, WL_FRAME_MAX_COBS_LEN, 256);
    peer.enable_capture();

    wirelink::astrial::SerialConfig adapter_config;
    adapter_config.port = terminal.slave_name();
    adapter_config.auto_reconnect = false;
    auto opened = wirelink::astrial::SerialAdapter::open(device.context, adapter_config);
    if (!opened) throw std::system_error(opened.error(), "open typed adapter");
    auto adapter = std::move(opened.value());

    std::array<joint_command_t, 6> joints{};
    for (std::size_t index = 0; index < joints.size(); ++index)
    {
        joint_command_clear(&joints[index]);
        joints[index].has_position_bits = true;
        joints[index].position_bits = 0x3F800000U + static_cast<uint32_t>(index);
        joints[index].has_velocity_bits = true;
        joints[index].velocity_bits = 0x40000000U + static_cast<uint32_t>(index);
        joints[index].has_torque_bits = true;
        joints[index].torque_bits = 0x40400000U + static_cast<uint32_t>(index);
        joints[index].has_kp_bits = true;
        joints[index].kp_bits = 0x40800000U + static_cast<uint32_t>(index);
        joints[index].has_kd_bits = true;
        joints[index].kd_bits = 0x40A00000U + static_cast<uint32_t>(index);
        joints[index].has_mode = true;
        joints[index].mode = MIT;
    }

    static constexpr std::string_view source{"desktop-control"};
    static constexpr std::array<uint8_t, 4> extension{0x00, 0xA5, 0x5A, 0xFF};
    arm_command_t command{};
    arm_command_clear(&command);
    command.joints = joints.data();
    command.joints_count = joints.size();
    command.joints_capacity = joints.size();
    command.has_sequence = true;
    command.sequence = 0x1020304050607080ULL;
    command.has_source = true;
    command.source = {source.data(), source.size()};
    command.has_extension = true;
    command.extension = {extension.data(), extension.size()};
    command.has_enabled = true;
    command.enabled = true;

    std::array<uint8_t, 256> payload{};
    std::size_t payload_length = 0;
    require(arm_command_encode(&command, payload.data(), payload.size(),
                               &payload_length) == WL_CODEC_OK,
            "generated ArmCommand encode failed");
    require(payload_length == arm_command_encoded_size(&command),
            "generated encoded size mismatch");

    auto transmit_from_peer = [&]() -> std::size_t
    {
        require(wl_send_unreliable(&peer.context, ARM_COMMAND_MESSAGE_ID,
                                   payload.data(), payload_length) == WL_OK,
                "typed peer encode failed");
        wl_event_t tx_event{};
        require(wl_poll(&peer.context, 0, &tx_event) == WL_OK &&
                    tx_event.type == WL_EVT_TX_SUCCESS,
                "typed peer TX event missing");
        const std::size_t wire_length = peer.outbound.size();
        terminal.write_all(peer.outbound);
        peer.outbound.clear();
        return wire_length;
    };

    const std::size_t first_wire_length = transmit_from_peer();
    wl_event_t event{};
    require(poll_until_event(*adapter, device, event, WL_EVT_UNRELIABLE_RX),
            "typed frame was not delivered");
    require(event.message_id == ARM_COMMAND_MESSAGE_ID, "typed message id mismatch");

    std::array<joint_command_t, 6> decoded_joints{};
    arm_command_t decoded{};
    decoded.joints = decoded_joints.data();
    decoded.joints_capacity = decoded_joints.size();
    require(arm_command_decode(event.payload, event.payload_len, &decoded) == WL_CODEC_OK,
            "generated ArmCommand decode failed");
    require(decoded.joints_count == joints.size(), "typed joint count mismatch");
    require(decoded.joints[5].kd_bits == joints[5].kd_bits,
            "typed nested joint payload mismatch");
    require(decoded.has_sequence && decoded.sequence == command.sequence,
            "typed sequence mismatch");
    require(decoded.has_source && decoded.source.length == source.size() &&
                std::memcmp(decoded.source.data, source.data(), source.size()) == 0,
            "typed source mismatch");
    require(decoded.has_extension && decoded.extension.length == extension.size() &&
                std::memcmp(decoded.extension.data, extension.data(), extension.size()) == 0,
            "typed extension mismatch");
    require(points_into(decoded.source.data, decoded.source.length,
                        event.payload, event.payload_len),
            "decoded string did not borrow the Wirelink event payload");
    require(points_into(decoded.extension.data, decoded.extension.length,
                        event.payload, event.payload_len),
            "decoded bytes did not borrow the Wirelink event payload");

    const std::size_t second_wire_length = transmit_from_peer();
    const auto expected_rx_bytes = first_wire_length + second_wire_length;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    wirelink::astrial::SerialAdapterStats stats;
    do
    {
        adapter->get_stats(stats);
        if (stats.rx_bytes >= expected_rx_bytes) break;
        require(adapter->service() == WL_OK, "typed adapter service failed");
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < deadline);
    require(stats.rx_bytes >= expected_rx_bytes,
            "second typed frame was not buffered during the event lease");
    require(std::memcmp(decoded.source.data, source.data(), source.size()) == 0,
            "leased string changed before event release");
    wl_event_t blocked{};
    require(wl_poll(&device.context, 1, &blocked) == WL_ERR_NO_DATA,
            "a second RX event escaped while the first event was leased");

    wl_event_release(&device.context, &event);
    event = {};
    require(poll_until_event(*adapter, device, event, WL_EVT_UNRELIABLE_RX),
            "typed RX did not resume after event release");
    wl_event_release(&device.context, &event);
}
}

int main()
{
    try
    {
        test_wirelink_round_trip();
        test_rx_backpressure_resume();
        test_generated_codec_borrows_event_payload();
        std::cout << "Wirelink Astrial virtual serial tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Wirelink Astrial virtual serial tests failed: " << error.what() << '\n';
        return 1;
    }
}

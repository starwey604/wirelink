/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/astrial/serial_adapter.hpp"
#include "control.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>

namespace
{
using namespace std::chrono_literals;

uint32_t float_bits(float value)
{
    uint32_t bits{};
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

struct LinkStorage
{
    wl_ctx_t context{};
    std::array<uint8_t, 256> tx_payload{};
    std::array<uint8_t, 320> tx_unit{};
    std::array<uint8_t, 64> control_unit{};
    std::array<uint8_t, 640> rx_fifo{};
    std::array<uint8_t, 320> rx_fallback{};
};

int initialize(LinkStorage& memory)
{
    const wl_config_t config{
        .max_payload_len = static_cast<uint16_t>(memory.tx_payload.size()),
        .envelope = WL_ENVELOPE_COBS_STREAM,
        .integrity = WL_INTEGRITY_CRC32C,
        .session_id = UINT64_C(0xA571A1),
        .max_retries = 2,
        .ack_timeout_ms = 20,
        .max_transmission_unit = memory.tx_unit.size(),
    };
    const wl_storage_t storage{
        .tx_payload = memory.tx_payload.data(),
        .tx_payload_size = memory.tx_payload.size(),
        .tx_unit = memory.tx_unit.data(),
        .tx_unit_size = memory.tx_unit.size(),
        .control_unit = memory.control_unit.data(),
        .control_unit_size = memory.control_unit.size(),
        .rx_fifo = memory.rx_fifo.data(),
        .rx_fifo_size = memory.rx_fifo.size(),
        .rx_fallback = memory.rx_fallback.data(),
        .rx_fallback_size = memory.rx_fallback.size(),
    };
    return wl_init(&memory.context, &config, &storage);
}
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: " << argv[0] << " SERIAL_PORT\n";
        return 2;
    }

    LinkStorage memory;
    if (const int result = initialize(memory); result != WL_OK)
    {
        std::cerr << "wl_init: " << wl_err_str(result) << '\n';
        return 1;
    }

    wirelink::astrial::SerialConfig serial_config;
    serial_config.port = argv[1];
    serial_config.baud_rate = 3000000;
    auto opened = wirelink::astrial::SerialAdapter::open(memory.context, serial_config);
    if (!opened)
    {
        std::cerr << "serial open: " << opened.error().message() << '\n';
        return 1;
    }
    auto adapter = std::move(opened.value());

    std::array<joint_command_t, 6> joints{};
    for (std::size_t index = 0; index < joints.size(); ++index)
    {
        auto& joint = joints[index];
        joint.has_position_bits = true;
        joint.position_bits = float_bits(static_cast<float>(index) * 0.1F);
        joint.has_velocity_bits = true;
        joint.velocity_bits = float_bits(0.0F);
        joint.has_torque_bits = true;
        joint.torque_bits = float_bits(0.0F);
        joint.has_kp_bits = true;
        joint.kp_bits = float_bits(20.0F);
        joint.has_kd_bits = true;
        joint.kd_bits = float_bits(0.5F);
        joint.has_mode = true;
        joint.mode = MIT;
    }

    arm_command_t command{};
    command.joints = joints.data();
    command.joints_count = joints.size();
    command.joints_capacity = joints.size();
    command.has_sequence = true;
    command.sequence = 1;
    command.has_enabled = true;
    command.enabled = true;

    std::array<uint8_t, 256> payload{};
    std::size_t payload_length{};
    if (arm_command_encode(&command, payload.data(), payload.size(),
                           &payload_length) != WL_CODEC_OK)
    {
        std::cerr << "ArmCommand encoding failed\n";
        return 1;
    }
    if (const int result = wl_send_unreliable(
            &memory.context, ARM_COMMAND_MESSAGE_ID, payload.data(), payload_length);
        result != WL_OK)
    {
        std::cerr << "send: " << wl_err_str(result) << '\n';
        return 1;
    }

    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        wl_event_t event{};
        const int polled = wl_poll(&memory.context, 0, &event);
        const int serviced = adapter->service();
        if (serviced != WL_OK && serviced != WL_ERR_WOULD_BLOCK)
        {
            std::cerr << "serial service: " << wl_err_str(serviced) << '\n';
            return 1;
        }
        if (polled == WL_OK && event.type == WL_EVT_TX_SUCCESS)
        {
            std::cout << "sent ArmCommand (" << payload_length << " payload bytes)\n";
            return 0;
        }
        if (polled != WL_OK && polled != WL_ERR_NO_DATA)
        {
            std::cerr << "poll: " << wl_err_str(polled) << '\n';
            return 1;
        }
        std::this_thread::sleep_for(1ms);
    }

    std::cerr << "timed out waiting for serial TX completion\n";
    return 1;
}

/* SPDX-License-Identifier: Apache-2.0 */
#include "common/packet_tool_io.hpp"
#include "control.h"
#include <array>
#include <cstring>
#include <iostream>
static uint32_t float_bits(float value) {
  uint32_t bits; static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits)); return bits;
}
int main(int argc, char **argv) {
  if (argc != 2) { std::cerr << "usage: " << argv[0] << " SERIAL_PORT\n"; return 2; }
  try {
    ExamplePacketIo io;
    io.serial(argv[1]);
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

    ExamplePacketIo::check(example_packet_tool_send(ARM_COMMAND_MESSAGE_ID,
      payload.data(), payload_length));
    const auto deadline = ExamplePacketIo::Clock::now() + std::chrono::seconds(2);
    while (ExamplePacketIo::Clock::now() < deadline) {
      io.service();
      if (example_packet_tool_sent()) {
        std::cout << "physically sent ArmCommand (" << payload_length << " payload bytes)\n";
        return 0;
      }
      io.wait(deadline);
    }
    throw std::runtime_error("serial physical TX completion timeout");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}

#ifndef WIRELINK_GENERATED_CONTROL_H
#define WIRELINK_GENERATED_CONTROL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wirelink/codec.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct joint_command joint_command_t;
typedef struct arm_command arm_command_t;

typedef int32_t joint_mode_t;
#define DISABLED INT32_C(0)
#define MIT INT32_C(1)

struct joint_command {
  bool has_position_bits;
  uint32_t position_bits;
  bool has_velocity_bits;
  uint32_t velocity_bits;
  bool has_torque_bits;
  uint32_t torque_bits;
  bool has_kp_bits;
  uint32_t kp_bits;
  bool has_kd_bits;
  uint32_t kd_bits;
  bool has_mode;
  joint_mode_t mode;
};

struct arm_command {
  joint_command_t *joints;
  size_t joints_count;
  size_t joints_capacity;
  bool has_sequence;
  uint64_t sequence;
  bool has_source;
  wl_codec_string_t source;
  bool has_extension;
  wl_codec_bytes_t extension;
  bool has_enabled;
  bool enabled;
};

#define JOINT_COMMAND_MESSAGE_ID 2U
void joint_command_clear(joint_command_t *value);
size_t joint_command_encoded_size(const joint_command_t *value);
wl_codec_status_t joint_command_encode(const joint_command_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t joint_command_decode(const uint8_t *input, size_t input_length, joint_command_t *out);

#define ARM_COMMAND_MESSAGE_ID 16U
void arm_command_clear(arm_command_t *value);
size_t arm_command_encoded_size(const arm_command_t *value);
wl_codec_status_t arm_command_encode(const arm_command_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t arm_command_decode(const uint8_t *input, size_t input_length, arm_command_t *out);

#ifdef __cplusplus
}
#endif

#endif

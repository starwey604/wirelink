#ifndef WIRELINK_GENERATED_CONTROL_H
#define WIRELINK_GENERATED_CONTROL_H

/* Advanced borrowed codec and explicit value/view conversions. */
#include "control_values.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct joint_command joint_command_t;
typedef struct arm_command arm_command_t;

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
};

void joint_command_clear(joint_command_t *value);
size_t joint_command_encoded_size(const joint_command_t *value);
wl_codec_status_t joint_command_encode(const joint_command_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t joint_command_decode(const uint8_t *input, size_t input_length, joint_command_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t joint_command_value_from_view(const joint_command_t *view, joint_command_value_t *out);
wl_codec_status_t joint_command_value_to_view(const joint_command_value_t *value, joint_command_t *out);

void arm_command_clear(arm_command_t *value);
size_t arm_command_encoded_size(const arm_command_t *value);
wl_codec_status_t arm_command_encode(const arm_command_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t arm_command_decode(const uint8_t *input, size_t input_length, arm_command_t *out);

#ifdef __cplusplus
}
#endif

#endif

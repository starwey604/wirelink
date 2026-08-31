#ifndef WIRELINK_GENERATED_CONTROL_H
#define WIRELINK_GENERATED_CONTROL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wirelink/codec.h>
#include <float.h>

#if defined(__cplusplus)
static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128 && FLT_MIN_EXP == -125, "WLC float32 requires IEEE-754 binary32");
#else
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128 && FLT_MIN_EXP == -125, "WLC float32 requires IEEE-754 binary32");
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct joint_command joint_command_t;
typedef struct arm_command arm_command_t;
typedef struct arm_mit_command arm_mit_command_t;
typedef struct home_request home_request_t;
typedef struct home_response home_response_t;

typedef int32_t joint_mode_t;
#define DISABLED INT32_C(0)
#define MIT INT32_C(1)

typedef int32_t operation_status_t;
#define OPERATION_OK INT32_C(0)
#define OPERATION_REJECTED INT32_C(1)

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

struct arm_mit_command {
  bool has_controls;
  float controls[30];
  bool has_sequence;
  uint64_t sequence;
  bool has_dt_s;
  float dt_s;
};

struct home_request {
  bool has_operation_id;
  uint32_t operation_id;
  bool has_joint_mask;
  uint32_t joint_mask;
};

struct home_response {
  bool has_operation_id;
  uint32_t operation_id;
  bool has_status;
  operation_status_t status;
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

#define ARM_MIT_COMMAND_MESSAGE_ID 17U
void arm_mit_command_clear(arm_mit_command_t *value);
size_t arm_mit_command_encoded_size(const arm_mit_command_t *value);
wl_codec_status_t arm_mit_command_encode(const arm_mit_command_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t arm_mit_command_decode(const uint8_t *input, size_t input_length, arm_mit_command_t *out);

#define HOME_REQUEST_MESSAGE_ID 18U
void home_request_clear(home_request_t *value);
size_t home_request_encoded_size(const home_request_t *value);
wl_codec_status_t home_request_encode(const home_request_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t home_request_decode(const uint8_t *input, size_t input_length, home_request_t *out);

#define HOME_RESPONSE_MESSAGE_ID 19U
void home_response_clear(home_response_t *value);
size_t home_response_encoded_size(const home_response_t *value);
wl_codec_status_t home_response_encode(const home_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t home_response_decode(const uint8_t *input, size_t input_length, home_response_t *out);

#ifdef __cplusplus
}
#endif

#endif

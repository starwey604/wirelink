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
typedef struct bulk_begin bulk_begin_t;
typedef struct bulk_chunk bulk_chunk_t;
typedef struct bulk_end bulk_end_t;
typedef struct bulk_abort bulk_abort_t;
typedef struct bulk_status bulk_status_t;

typedef int32_t joint_mode_t;
#define DISABLED INT32_C(0)
#define MIT INT32_C(1)

typedef int32_t operation_status_t;
#define OPERATION_OK INT32_C(0)
#define OPERATION_REJECTED INT32_C(1)

typedef int32_t control_bulk_phase_t;
#define CONTROL_BULK_PHASE_NONE INT32_C(0)
#define CONTROL_BULK_PHASE_BEGIN INT32_C(1)
#define CONTROL_BULK_PHASE_CHUNK INT32_C(2)
#define CONTROL_BULK_PHASE_END INT32_C(3)
#define CONTROL_BULK_PHASE_ABORT INT32_C(4)

typedef int32_t control_bulk_status_code_t;
#define CONTROL_BULK_STATUS_OK INT32_C(0)
#define CONTROL_BULK_STATUS_BUSY INT32_C(1)
#define CONTROL_BULK_STATUS_OUT_OF_ORDER INT32_C(2)
#define CONTROL_BULK_STATUS_CONFLICT INT32_C(3)
#define CONTROL_BULK_STATUS_INVALID INT32_C(4)
#define CONTROL_BULK_STATUS_WRITE_FAILED INT32_C(5)
#define CONTROL_BULK_STATUS_INTEGRITY_FAILED INT32_C(6)
#define CONTROL_BULK_STATUS_ABORTED INT32_C(7)
#define CONTROL_BULK_STATUS_TIMED_OUT INT32_C(8)

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

struct bulk_begin {
  bool has_transfer_id;
  uint32_t transfer_id;
  bool has_total_length;
  uint64_t total_length;
  bool has_requested_chunk_size;
  uint32_t requested_chunk_size;
  bool has_object_crc32c;
  uint32_t object_crc32c;
};

struct bulk_chunk {
  bool has_transfer_id;
  uint32_t transfer_id;
  bool has_offset;
  uint64_t offset;
  bool has_data;
  wl_codec_bytes_t data;
};

struct bulk_end {
  bool has_transfer_id;
  uint32_t transfer_id;
  bool has_total_length;
  uint64_t total_length;
  bool has_object_crc32c;
  uint32_t object_crc32c;
};

struct bulk_abort {
  bool has_transfer_id;
  uint32_t transfer_id;
  bool has_reason;
  int32_t reason;
};

struct bulk_status {
  bool has_transfer_id;
  uint32_t transfer_id;
  bool has_phase;
  control_bulk_phase_t phase;
  bool has_code;
  control_bulk_status_code_t code;
  bool has_next_offset;
  uint64_t next_offset;
  bool has_accepted_chunk_size;
  uint32_t accepted_chunk_size;
};

#define JOINT_COMMAND_MESSAGE_ID 2U
#define JOINT_COMMAND_HAS_MAX_ENCODED_SIZE 1
#define JOINT_COMMAND_MAX_ENCODED_SIZE UINT64_C(31)
void joint_command_clear(joint_command_t *value);
size_t joint_command_encoded_size(const joint_command_t *value);
wl_codec_status_t joint_command_encode(const joint_command_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t joint_command_decode(const uint8_t *input, size_t input_length, joint_command_t *out);

#define ARM_COMMAND_MESSAGE_ID 16U
#define ARM_COMMAND_HAS_MAX_ENCODED_SIZE 0
void arm_command_clear(arm_command_t *value);
size_t arm_command_encoded_size(const arm_command_t *value);
wl_codec_status_t arm_command_encode(const arm_command_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t arm_command_decode(const uint8_t *input, size_t input_length, arm_command_t *out);

#define ARM_MIT_COMMAND_MESSAGE_ID 17U
#define ARM_MIT_COMMAND_HAS_MAX_ENCODED_SIZE 1
#define ARM_MIT_COMMAND_MAX_ENCODED_SIZE UINT64_C(138)
void arm_mit_command_clear(arm_mit_command_t *value);
size_t arm_mit_command_encoded_size(const arm_mit_command_t *value);
wl_codec_status_t arm_mit_command_encode(const arm_mit_command_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t arm_mit_command_decode(const uint8_t *input, size_t input_length, arm_mit_command_t *out);

#define HOME_REQUEST_MESSAGE_ID 18U
#define HOME_REQUEST_HAS_MAX_ENCODED_SIZE 1
#define HOME_REQUEST_MAX_ENCODED_SIZE UINT64_C(12)
void home_request_clear(home_request_t *value);
size_t home_request_encoded_size(const home_request_t *value);
wl_codec_status_t home_request_encode(const home_request_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t home_request_decode(const uint8_t *input, size_t input_length, home_request_t *out);

#define HOME_RESPONSE_MESSAGE_ID 19U
#define HOME_RESPONSE_HAS_MAX_ENCODED_SIZE 1
#define HOME_RESPONSE_MAX_ENCODED_SIZE UINT64_C(12)
void home_response_clear(home_response_t *value);
size_t home_response_encoded_size(const home_response_t *value);
wl_codec_status_t home_response_encode(const home_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t home_response_decode(const uint8_t *input, size_t input_length, home_response_t *out);

#define BULK_BEGIN_MESSAGE_ID 32U
#define BULK_BEGIN_HAS_MAX_ENCODED_SIZE 1
#define BULK_BEGIN_MAX_ENCODED_SIZE UINT64_C(24)
void bulk_begin_clear(bulk_begin_t *value);
size_t bulk_begin_encoded_size(const bulk_begin_t *value);
wl_codec_status_t bulk_begin_encode(const bulk_begin_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t bulk_begin_decode(const uint8_t *input, size_t input_length, bulk_begin_t *out);

#define BULK_CHUNK_MESSAGE_ID 33U
#define BULK_CHUNK_HAS_MAX_ENCODED_SIZE 0
void bulk_chunk_clear(bulk_chunk_t *value);
size_t bulk_chunk_encoded_size(const bulk_chunk_t *value);
wl_codec_status_t bulk_chunk_encode(const bulk_chunk_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t bulk_chunk_decode(const uint8_t *input, size_t input_length, bulk_chunk_t *out);

#define BULK_END_MESSAGE_ID 34U
#define BULK_END_HAS_MAX_ENCODED_SIZE 1
#define BULK_END_MAX_ENCODED_SIZE UINT64_C(19)
void bulk_end_clear(bulk_end_t *value);
size_t bulk_end_encoded_size(const bulk_end_t *value);
wl_codec_status_t bulk_end_encode(const bulk_end_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t bulk_end_decode(const uint8_t *input, size_t input_length, bulk_end_t *out);

#define BULK_ABORT_MESSAGE_ID 35U
#define BULK_ABORT_HAS_MAX_ENCODED_SIZE 1
#define BULK_ABORT_MAX_ENCODED_SIZE UINT64_C(11)
void bulk_abort_clear(bulk_abort_t *value);
size_t bulk_abort_encoded_size(const bulk_abort_t *value);
wl_codec_status_t bulk_abort_encode(const bulk_abort_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t bulk_abort_decode(const uint8_t *input, size_t input_length, bulk_abort_t *out);

#define BULK_STATUS_MESSAGE_ID 36U
#define BULK_STATUS_HAS_MAX_ENCODED_SIZE 1
#define BULK_STATUS_MAX_ENCODED_SIZE UINT64_C(31)
void bulk_status_clear(bulk_status_t *value);
size_t bulk_status_encoded_size(const bulk_status_t *value);
wl_codec_status_t bulk_status_encode(const bulk_status_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t bulk_status_decode(const uint8_t *input, size_t input_length, bulk_status_t *out);

#ifdef __cplusplus
}
#endif

#endif

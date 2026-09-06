#ifndef WIRELINK_GENERATED_CONTROL_H_VALUES
#define WIRELINK_GENERATED_CONTROL_H_VALUES

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

/* Self-owning business values. Assignment copies all data; no destroy is needed.
 * String lengths are bytes (embedded NUL is allowed); data[length] is a
 * convenience terminator after clear/decode/from_view, not part of the wire.
 * Views borrow their source. Conversion input/output must not overlap.
 * Failed conversions/decodes leave output unchanged. */
#define JOINT_COMMAND_HAS_VALUE 1
typedef struct {
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
} joint_command_value_t;
#define JOINT_COMMAND_VALUE_SIZE (sizeof(joint_command_value_t))
void joint_command_value_clear(joint_command_value_t *value);
size_t joint_command_value_encoded_size(const joint_command_value_t *value);
wl_codec_status_t joint_command_value_encode(const joint_command_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t joint_command_value_decode(const uint8_t *input, size_t length, joint_command_value_t *out);

#define ARM_COMMAND_HAS_VALUE 0
#define ARM_MIT_COMMAND_HAS_VALUE 1
typedef struct {
  bool has_controls;
  float controls[30];
  bool has_sequence;
  uint64_t sequence;
  bool has_dt_s;
  float dt_s;
} arm_mit_command_value_t;
#define ARM_MIT_COMMAND_VALUE_SIZE (sizeof(arm_mit_command_value_t))
void arm_mit_command_value_clear(arm_mit_command_value_t *value);
size_t arm_mit_command_value_encoded_size(const arm_mit_command_value_t *value);
wl_codec_status_t arm_mit_command_value_encode(const arm_mit_command_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t arm_mit_command_value_decode(const uint8_t *input, size_t length, arm_mit_command_value_t *out);

#define HOME_REQUEST_HAS_VALUE 1
typedef struct {
  bool has_operation_id;
  uint32_t operation_id;
  bool has_joint_mask;
  uint32_t joint_mask;
} home_request_value_t;
#define HOME_REQUEST_VALUE_SIZE (sizeof(home_request_value_t))
void home_request_value_clear(home_request_value_t *value);
size_t home_request_value_encoded_size(const home_request_value_t *value);
wl_codec_status_t home_request_value_encode(const home_request_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t home_request_value_decode(const uint8_t *input, size_t length, home_request_value_t *out);

#define HOME_RESPONSE_HAS_VALUE 1
typedef struct {
  bool has_operation_id;
  uint32_t operation_id;
  bool has_status;
  operation_status_t status;
} home_response_value_t;
#define HOME_RESPONSE_VALUE_SIZE (sizeof(home_response_value_t))
void home_response_value_clear(home_response_value_t *value);
size_t home_response_value_encoded_size(const home_response_value_t *value);
wl_codec_status_t home_response_value_encode(const home_response_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t home_response_value_decode(const uint8_t *input, size_t length, home_response_value_t *out);

#define BULK_BEGIN_HAS_VALUE 1
typedef struct {
  bool has_transfer_id;
  uint32_t transfer_id;
  bool has_total_length;
  uint64_t total_length;
  bool has_requested_chunk_size;
  uint32_t requested_chunk_size;
  bool has_object_crc32c;
  uint32_t object_crc32c;
} bulk_begin_value_t;
#define BULK_BEGIN_VALUE_SIZE (sizeof(bulk_begin_value_t))
void bulk_begin_value_clear(bulk_begin_value_t *value);
size_t bulk_begin_value_encoded_size(const bulk_begin_value_t *value);
wl_codec_status_t bulk_begin_value_encode(const bulk_begin_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t bulk_begin_value_decode(const uint8_t *input, size_t length, bulk_begin_value_t *out);

#define BULK_CHUNK_HAS_VALUE 1
typedef struct {
  bool has_transfer_id;
  uint32_t transfer_id;
  bool has_offset;
  uint64_t offset;
  bool has_data;
  struct { size_t length; uint8_t data[4096]; } data;
} bulk_chunk_value_t;
#define BULK_CHUNK_VALUE_SIZE (sizeof(bulk_chunk_value_t))
void bulk_chunk_value_clear(bulk_chunk_value_t *value);
size_t bulk_chunk_value_encoded_size(const bulk_chunk_value_t *value);
wl_codec_status_t bulk_chunk_value_encode(const bulk_chunk_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t bulk_chunk_value_decode(const uint8_t *input, size_t length, bulk_chunk_value_t *out);

#define BULK_END_HAS_VALUE 1
typedef struct {
  bool has_transfer_id;
  uint32_t transfer_id;
  bool has_total_length;
  uint64_t total_length;
  bool has_object_crc32c;
  uint32_t object_crc32c;
} bulk_end_value_t;
#define BULK_END_VALUE_SIZE (sizeof(bulk_end_value_t))
void bulk_end_value_clear(bulk_end_value_t *value);
size_t bulk_end_value_encoded_size(const bulk_end_value_t *value);
wl_codec_status_t bulk_end_value_encode(const bulk_end_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t bulk_end_value_decode(const uint8_t *input, size_t length, bulk_end_value_t *out);

#define BULK_ABORT_HAS_VALUE 1
typedef struct {
  bool has_transfer_id;
  uint32_t transfer_id;
  bool has_reason;
  int32_t reason;
} bulk_abort_value_t;
#define BULK_ABORT_VALUE_SIZE (sizeof(bulk_abort_value_t))
void bulk_abort_value_clear(bulk_abort_value_t *value);
size_t bulk_abort_value_encoded_size(const bulk_abort_value_t *value);
wl_codec_status_t bulk_abort_value_encode(const bulk_abort_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t bulk_abort_value_decode(const uint8_t *input, size_t length, bulk_abort_value_t *out);

#define BULK_STATUS_HAS_VALUE 1
typedef struct {
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
} bulk_status_value_t;
#define BULK_STATUS_VALUE_SIZE (sizeof(bulk_status_value_t))
void bulk_status_value_clear(bulk_status_value_t *value);
size_t bulk_status_value_encoded_size(const bulk_status_value_t *value);
wl_codec_status_t bulk_status_value_encode(const bulk_status_value_t *value, uint8_t *out, size_t capacity, size_t *length);
wl_codec_status_t bulk_status_value_decode(const uint8_t *input, size_t length, bulk_status_value_t *out);

#define JOINT_COMMAND_MESSAGE_ID 2U
#define JOINT_COMMAND_HAS_MAX_ENCODED_SIZE 1
#define JOINT_COMMAND_MAX_ENCODED_SIZE UINT64_C(31)
#define ARM_COMMAND_MESSAGE_ID 16U
#define ARM_COMMAND_HAS_MAX_ENCODED_SIZE 0
#define ARM_MIT_COMMAND_MESSAGE_ID 17U
#define ARM_MIT_COMMAND_HAS_MAX_ENCODED_SIZE 1
#define ARM_MIT_COMMAND_MAX_ENCODED_SIZE UINT64_C(138)
#define HOME_REQUEST_MESSAGE_ID 18U
#define HOME_REQUEST_HAS_MAX_ENCODED_SIZE 1
#define HOME_REQUEST_MAX_ENCODED_SIZE UINT64_C(12)
#define HOME_RESPONSE_MESSAGE_ID 19U
#define HOME_RESPONSE_HAS_MAX_ENCODED_SIZE 1
#define HOME_RESPONSE_MAX_ENCODED_SIZE UINT64_C(12)
#define BULK_BEGIN_MESSAGE_ID 32U
#define BULK_BEGIN_HAS_MAX_ENCODED_SIZE 1
#define BULK_BEGIN_MAX_ENCODED_SIZE UINT64_C(24)
#define BULK_CHUNK_MESSAGE_ID 33U
#define BULK_CHUNK_HAS_MAX_ENCODED_SIZE 1
#define BULK_CHUNK_MAX_ENCODED_SIZE UINT64_C(4113)
#define BULK_END_MESSAGE_ID 34U
#define BULK_END_HAS_MAX_ENCODED_SIZE 1
#define BULK_END_MAX_ENCODED_SIZE UINT64_C(19)
#define BULK_ABORT_MESSAGE_ID 35U
#define BULK_ABORT_HAS_MAX_ENCODED_SIZE 1
#define BULK_ABORT_MAX_ENCODED_SIZE UINT64_C(11)
#define BULK_STATUS_MESSAGE_ID 36U
#define BULK_STATUS_HAS_MAX_ENCODED_SIZE 1
#define BULK_STATUS_MAX_ENCODED_SIZE UINT64_C(31)
#ifdef __cplusplus
}
#endif

#endif

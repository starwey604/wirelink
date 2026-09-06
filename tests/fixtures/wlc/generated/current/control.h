#ifndef WIRELINK_GENERATED_CONTROL_H
#define WIRELINK_GENERATED_CONTROL_H

/* Advanced borrowed codec and explicit value/view conversions. */
#include "control_values.h"

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

void arm_mit_command_clear(arm_mit_command_t *value);
size_t arm_mit_command_encoded_size(const arm_mit_command_t *value);
wl_codec_status_t arm_mit_command_encode(const arm_mit_command_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t arm_mit_command_decode(const uint8_t *input, size_t input_length, arm_mit_command_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t arm_mit_command_value_from_view(const arm_mit_command_t *view, arm_mit_command_value_t *out);
wl_codec_status_t arm_mit_command_value_to_view(const arm_mit_command_value_t *value, arm_mit_command_t *out);

void home_request_clear(home_request_t *value);
size_t home_request_encoded_size(const home_request_t *value);
wl_codec_status_t home_request_encode(const home_request_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t home_request_decode(const uint8_t *input, size_t input_length, home_request_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t home_request_value_from_view(const home_request_t *view, home_request_value_t *out);
wl_codec_status_t home_request_value_to_view(const home_request_value_t *value, home_request_t *out);

void home_response_clear(home_response_t *value);
size_t home_response_encoded_size(const home_response_t *value);
wl_codec_status_t home_response_encode(const home_response_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t home_response_decode(const uint8_t *input, size_t input_length, home_response_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t home_response_value_from_view(const home_response_t *view, home_response_value_t *out);
wl_codec_status_t home_response_value_to_view(const home_response_value_t *value, home_response_t *out);

void bulk_begin_clear(bulk_begin_t *value);
size_t bulk_begin_encoded_size(const bulk_begin_t *value);
wl_codec_status_t bulk_begin_encode(const bulk_begin_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t bulk_begin_decode(const uint8_t *input, size_t input_length, bulk_begin_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t bulk_begin_value_from_view(const bulk_begin_t *view, bulk_begin_value_t *out);
wl_codec_status_t bulk_begin_value_to_view(const bulk_begin_value_t *value, bulk_begin_t *out);

void bulk_chunk_clear(bulk_chunk_t *value);
size_t bulk_chunk_encoded_size(const bulk_chunk_t *value);
wl_codec_status_t bulk_chunk_encode(const bulk_chunk_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t bulk_chunk_decode(const uint8_t *input, size_t input_length, bulk_chunk_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t bulk_chunk_value_from_view(const bulk_chunk_t *view, bulk_chunk_value_t *out);
wl_codec_status_t bulk_chunk_value_to_view(const bulk_chunk_value_t *value, bulk_chunk_t *out);

void bulk_end_clear(bulk_end_t *value);
size_t bulk_end_encoded_size(const bulk_end_t *value);
wl_codec_status_t bulk_end_encode(const bulk_end_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t bulk_end_decode(const uint8_t *input, size_t input_length, bulk_end_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t bulk_end_value_from_view(const bulk_end_t *view, bulk_end_value_t *out);
wl_codec_status_t bulk_end_value_to_view(const bulk_end_value_t *value, bulk_end_t *out);

void bulk_abort_clear(bulk_abort_t *value);
size_t bulk_abort_encoded_size(const bulk_abort_t *value);
wl_codec_status_t bulk_abort_encode(const bulk_abort_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t bulk_abort_decode(const uint8_t *input, size_t input_length, bulk_abort_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t bulk_abort_value_from_view(const bulk_abort_t *view, bulk_abort_value_t *out);
wl_codec_status_t bulk_abort_value_to_view(const bulk_abort_value_t *value, bulk_abort_t *out);

void bulk_status_clear(bulk_status_t *value);
size_t bulk_status_encoded_size(const bulk_status_t *value);
wl_codec_status_t bulk_status_encode(const bulk_status_t *value, uint8_t *out, size_t out_capacity, size_t *out_length);
wl_codec_status_t bulk_status_decode(const uint8_t *input, size_t input_length, bulk_status_t *out);

/* Advanced conversions: views borrow value/input storage. Inputs and outputs
 * must not overlap; failures leave output unchanged. */
wl_codec_status_t bulk_status_value_from_view(const bulk_status_t *view, bulk_status_value_t *out);
wl_codec_status_t bulk_status_value_to_view(const bulk_status_value_t *value, bulk_status_t *out);

#ifdef __cplusplus
}
#endif

#endif

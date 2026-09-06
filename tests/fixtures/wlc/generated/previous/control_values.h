#ifndef WIRELINK_GENERATED_CONTROL_H_VALUES
#define WIRELINK_GENERATED_CONTROL_H_VALUES

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wirelink/codec.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t joint_mode_t;
#define DISABLED INT32_C(0)
#define MIT INT32_C(1)

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
#define JOINT_COMMAND_MESSAGE_ID 2U
#define JOINT_COMMAND_HAS_MAX_ENCODED_SIZE 1
#define JOINT_COMMAND_MAX_ENCODED_SIZE UINT64_C(31)
#define ARM_COMMAND_MESSAGE_ID 16U
#define ARM_COMMAND_HAS_MAX_ENCODED_SIZE 0
#ifdef __cplusplus
}
#endif

#endif

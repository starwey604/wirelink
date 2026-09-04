#ifndef WIRELINK_GENERATED_CONTROL_H_BINDINGS
#define WIRELINK_GENERATED_CONTROL_H_BINDINGS

#include "control.h"
#include <wirelink/link.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t control_dispatch_domain_t;
enum {
  CONTROL_DISPATCH_OK = 0,
  CONTROL_DISPATCH_NON_RX,
  CONTROL_DISPATCH_UNKNOWN_MESSAGE,
  CONTROL_DISPATCH_MISSING_ROUTE,
  CONTROL_DISPATCH_MISSING_SCRATCH,
  CONTROL_DISPATCH_CODEC_ERROR,
  CONTROL_DISPATCH_HANDLER_ERROR,
  CONTROL_DISPATCH_INVALID_ARGUMENT
};

typedef struct {
  control_dispatch_domain_t domain;
  uint16_t message_id;
  wl_event_type_t event_type;
  wl_codec_status_t codec_status;
  int32_t handler_result;
} control_dispatch_result_t;

typedef struct {
  uint32_t delivered;
  uint32_t non_rx;
  uint32_t unknown_message;
  uint32_t missing_route;
  uint32_t missing_scratch;
  uint32_t codec_failure;
  uint32_t handler_failure;
} control_dispatch_counters_t;

typedef int32_t control_send_domain_t;
enum {
  CONTROL_SEND_OK = 0,
  CONTROL_SEND_CODEC_ERROR,
  CONTROL_SEND_CORE_ERROR
};

typedef struct {
  uint8_t *data;
  size_t capacity;
} control_encode_scratch_t;

typedef struct {
  control_send_domain_t domain;
  wl_codec_status_t codec_status;
  int core_result;
  int abort_result;
  uint32_t core_called;
  size_t payload_length;
  wl_tx_handle_t handle;
} control_send_result_t;

typedef int32_t (*control_joint_command_handler_fn)(void *user_data, const joint_command_t *message, wl_delivery_t delivery);
typedef struct {
  joint_command_t *scratch;
  control_joint_command_handler_fn handler;
  void *user_data;
} control_joint_command_route_t;

typedef int32_t (*control_arm_command_handler_fn)(void *user_data, const arm_command_t *message, wl_delivery_t delivery);
typedef struct {
  arm_command_t *scratch;
  control_arm_command_handler_fn handler;
  void *user_data;
} control_arm_command_route_t;

typedef struct {
  control_joint_command_route_t joint_command;
  control_arm_command_route_t arm_command;
  control_dispatch_counters_t counters;
} control_router_t;

control_dispatch_result_t control_dispatch_event(wl_ctx_t *ctx, const wl_event_t *event, control_router_t *router);

control_send_result_t control_joint_command_send_unreliable(wl_ctx_t *ctx, const joint_command_t *message, control_encode_scratch_t scratch);
control_send_result_t control_joint_command_send_reliable(wl_ctx_t *ctx, const joint_command_t *message, control_encode_scratch_t scratch);
control_send_result_t control_joint_command_send_direct(wl_ctx_t *ctx, const joint_command_t *message, wl_delivery_t delivery);

control_send_result_t control_arm_command_send_unreliable(wl_ctx_t *ctx, const arm_command_t *message, control_encode_scratch_t scratch);
control_send_result_t control_arm_command_send_reliable(wl_ctx_t *ctx, const arm_command_t *message, control_encode_scratch_t scratch);
control_send_result_t control_arm_command_send_direct(wl_ctx_t *ctx, const arm_command_t *message, wl_delivery_t delivery);

#ifdef __cplusplus
}
#endif

#endif

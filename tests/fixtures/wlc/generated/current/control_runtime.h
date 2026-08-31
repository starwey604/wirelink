#ifndef WIRELINK_GENERATED_CONTROL_RUNTIME_H
#define WIRELINK_GENERATED_CONTROL_RUNTIME_H

#include "control_bindings.h"
#include <wirelink/fifo.h>
#include <wirelink/latest.h>
#include <wirelink/rpc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONTROL_SCHEMA_IDENTITY UINT64_C(0x3E4D43CC60BB9E4E)
#define CONTROL_BINDING_PROFILE_IDENTITY UINT64_C(0x3BAB7A3869C4D89E)
#define CONTROL_BINDING_PROFILE_VERSION 1U
#define CONTROL_IDENTITY_ALGORITHM "fnv1a64-v1"

typedef int32_t control_runtime_domain_t;
enum {
  CONTROL_RUNTIME_OK = 0,
  CONTROL_RUNTIME_NON_RX,
  CONTROL_RUNTIME_UNKNOWN_MESSAGE,
  CONTROL_RUNTIME_MISSING_ROUTE,
  CONTROL_RUNTIME_DELIVERY_MISMATCH,
  CONTROL_RUNTIME_CODEC_ERROR,
  CONTROL_RUNTIME_STORAGE_ERROR,
  CONTROL_RUNTIME_RPC_ERROR,
  CONTROL_RUNTIME_CORE_ERROR,
  CONTROL_RUNTIME_APPLICATION_ERROR,
  CONTROL_RUNTIME_INVALID_ARGUMENT
};

typedef struct {
  control_runtime_domain_t domain;
  uint16_t message_id;
  wl_event_type_t event_type;
  wl_codec_status_t codec_status;
  int32_t storage_result;
  int32_t abort_result;
  wl_rpc_err_t rpc_result;
  int32_t core_result;
  int32_t application_result;
  wl_rpc_server_disposition_t rpc_disposition;
  uint32_t operation_id;
  wl_tx_handle_t handle;
  size_t payload_length;
  wl_rpc_server_response_t server_response;
} control_runtime_result_t;

typedef struct {
  uint8_t _reserved;
  wl_fifo_t *joint_command_fifo;
  wl_latest_t *arm_mit_command_latest;
} control_runtime_t;

control_runtime_result_t control_runtime_dispatch_event(wl_ctx_t *ctx, const wl_event_t *event, control_runtime_t *runtime);

#ifdef __cplusplus
}
#endif

#endif

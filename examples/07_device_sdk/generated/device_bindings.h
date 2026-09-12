#ifndef WIRELINK_GENERATED_DEVICE_H_BINDINGS
#define WIRELINK_GENERATED_DEVICE_H_BINDINGS

#include "device.h"
#include <wirelink/link.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t device_dispatch_domain_t;
enum {
  DEVICE_DISPATCH_OK = 0,
  DEVICE_DISPATCH_NON_RX,
  DEVICE_DISPATCH_UNKNOWN_MESSAGE,
  DEVICE_DISPATCH_MISSING_ROUTE,
  DEVICE_DISPATCH_MISSING_SCRATCH,
  DEVICE_DISPATCH_CODEC_ERROR,
  DEVICE_DISPATCH_HANDLER_ERROR,
  DEVICE_DISPATCH_INVALID_ARGUMENT
};

typedef struct {
  device_dispatch_domain_t domain;
  uint16_t message_id;
  wl_event_type_t event_type;
  wl_codec_status_t codec_status;
  int32_t handler_result;
} device_dispatch_result_t;

typedef struct {
  uint32_t delivered;
  uint32_t non_rx;
  uint32_t unknown_message;
  uint32_t missing_route;
  uint32_t missing_scratch;
  uint32_t codec_failure;
  uint32_t handler_failure;
} device_dispatch_counters_t;

typedef int32_t device_send_domain_t;
enum {
  DEVICE_SEND_OK = 0,
  DEVICE_SEND_CODEC_ERROR,
  DEVICE_SEND_CORE_ERROR
};

typedef struct {
  uint8_t *data;
  size_t capacity;
} device_encode_scratch_t;

typedef struct {
  device_send_domain_t domain;
  wl_codec_status_t codec_status;
  int core_result;
  size_t payload_length;
  wl_tx_handle_t handle;
} device_send_result_t;

typedef int32_t (*device_info_request_handler_fn)(void *user_data, const info_request_t *message, wl_delivery_t delivery);
typedef struct {
  info_request_t *scratch;
  device_info_request_handler_fn handler;
  void *user_data;
} device_info_request_route_t;

typedef int32_t (*device_settings_handler_fn)(void *user_data, const settings_t *message, wl_delivery_t delivery);
typedef struct {
  settings_t *scratch;
  device_settings_handler_fn handler;
  void *user_data;
} device_settings_route_t;

typedef int32_t (*device_info_response_handler_fn)(void *user_data, const info_response_t *message, wl_delivery_t delivery);
typedef struct {
  info_response_t *scratch;
  device_info_response_handler_fn handler;
  void *user_data;
} device_info_response_route_t;

typedef int32_t (*device_configure_request_handler_fn)(void *user_data, const configure_request_t *message, wl_delivery_t delivery);
typedef struct {
  configure_request_t *scratch;
  device_configure_request_handler_fn handler;
  void *user_data;
} device_configure_request_route_t;

typedef int32_t (*device_configure_response_handler_fn)(void *user_data, const configure_response_t *message, wl_delivery_t delivery);
typedef struct {
  configure_response_t *scratch;
  device_configure_response_handler_fn handler;
  void *user_data;
} device_configure_response_route_t;

typedef int32_t (*device_numbers_handler_fn)(void *user_data, const numbers_t *message, wl_delivery_t delivery);
typedef struct {
  numbers_t *scratch;
  device_numbers_handler_fn handler;
  void *user_data;
} device_numbers_route_t;

typedef struct {
  device_info_request_route_t info_request;
  device_settings_route_t settings;
  device_info_response_route_t info_response;
  device_configure_request_route_t configure_request;
  device_configure_response_route_t configure_response;
  device_numbers_route_t numbers;
  device_dispatch_counters_t counters;
} device_router_t;

device_dispatch_result_t device_dispatch_event(wl_ctx_t *ctx, const wl_event_t *event, device_router_t *router);

/* Encodes directly into Wirelink-owned TX storage. */
device_send_result_t device_info_request_send(wl_ctx_t *ctx, const info_request_t *message, wl_delivery_t delivery, wl_time_ms_t now_ms);

/* Encodes directly into Wirelink-owned TX storage. */
device_send_result_t device_settings_send(wl_ctx_t *ctx, const settings_t *message, wl_delivery_t delivery, wl_time_ms_t now_ms);

/* Encodes directly into Wirelink-owned TX storage. */
device_send_result_t device_info_response_send(wl_ctx_t *ctx, const info_response_t *message, wl_delivery_t delivery, wl_time_ms_t now_ms);

/* Encodes directly into Wirelink-owned TX storage. */
device_send_result_t device_configure_request_send(wl_ctx_t *ctx, const configure_request_t *message, wl_delivery_t delivery, wl_time_ms_t now_ms);

/* Encodes directly into Wirelink-owned TX storage. */
device_send_result_t device_configure_response_send(wl_ctx_t *ctx, const configure_response_t *message, wl_delivery_t delivery, wl_time_ms_t now_ms);

/* Encodes directly into Wirelink-owned TX storage. */
device_send_result_t device_numbers_send(wl_ctx_t *ctx, const numbers_t *message, wl_delivery_t delivery, wl_time_ms_t now_ms);

#ifdef __cplusplus
}
#endif

#endif

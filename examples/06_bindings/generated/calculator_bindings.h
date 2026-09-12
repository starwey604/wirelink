#ifndef WIRELINK_GENERATED_CALCULATOR_H_BINDINGS
#define WIRELINK_GENERATED_CALCULATOR_H_BINDINGS

#include "calculator.h"
#include <wirelink/link.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t calculator_dispatch_domain_t;
enum {
  CALCULATOR_DISPATCH_OK = 0,
  CALCULATOR_DISPATCH_NON_RX,
  CALCULATOR_DISPATCH_UNKNOWN_MESSAGE,
  CALCULATOR_DISPATCH_MISSING_ROUTE,
  CALCULATOR_DISPATCH_MISSING_SCRATCH,
  CALCULATOR_DISPATCH_CODEC_ERROR,
  CALCULATOR_DISPATCH_HANDLER_ERROR,
  CALCULATOR_DISPATCH_INVALID_ARGUMENT
};

typedef struct {
  calculator_dispatch_domain_t domain;
  uint16_t message_id;
  wl_event_type_t event_type;
  wl_codec_status_t codec_status;
  int32_t handler_result;
} calculator_dispatch_result_t;

typedef struct {
  uint32_t delivered;
  uint32_t non_rx;
  uint32_t unknown_message;
  uint32_t missing_route;
  uint32_t missing_scratch;
  uint32_t codec_failure;
  uint32_t handler_failure;
} calculator_dispatch_counters_t;

typedef int32_t calculator_send_domain_t;
enum {
  CALCULATOR_SEND_OK = 0,
  CALCULATOR_SEND_CODEC_ERROR,
  CALCULATOR_SEND_CORE_ERROR
};

typedef struct {
  uint8_t *data;
  size_t capacity;
} calculator_encode_scratch_t;

typedef struct {
  calculator_send_domain_t domain;
  wl_codec_status_t codec_status;
  int core_result;
  size_t payload_length;
  wl_tx_handle_t handle;
} calculator_send_result_t;

typedef int32_t (*calculator_add_request_handler_fn)(void *user_data, const add_request_t *message, wl_delivery_t delivery);
typedef struct {
  add_request_t *scratch;
  calculator_add_request_handler_fn handler;
  void *user_data;
} calculator_add_request_route_t;

typedef int32_t (*calculator_add_response_handler_fn)(void *user_data, const add_response_t *message, wl_delivery_t delivery);
typedef struct {
  add_response_t *scratch;
  calculator_add_response_handler_fn handler;
  void *user_data;
} calculator_add_response_route_t;

typedef struct {
  calculator_add_request_route_t add_request;
  calculator_add_response_route_t add_response;
  calculator_dispatch_counters_t counters;
} calculator_router_t;

calculator_dispatch_result_t calculator_dispatch_event(wl_ctx_t *ctx, const wl_event_t *event, calculator_router_t *router);

/* Encodes directly into Wirelink-owned TX storage. */
calculator_send_result_t calculator_add_request_send(wl_ctx_t *ctx, const add_request_t *message, wl_delivery_t delivery, wl_time_ms_t now_ms);

/* Encodes directly into Wirelink-owned TX storage. */
calculator_send_result_t calculator_add_response_send(wl_ctx_t *ctx, const add_response_t *message, wl_delivery_t delivery, wl_time_ms_t now_ms);

#ifdef __cplusplus
}
#endif

#endif

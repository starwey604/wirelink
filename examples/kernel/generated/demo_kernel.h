/* SPDX-License-Identifier: Apache-2.0 */
#ifndef DEMO_KERNEL_H
#define DEMO_KERNEL_H
#include "demo.h"
#include "wirelink/kernel.h"
#if WL_KERNEL_CONTRACT != 1
#error "static kernel contract mismatch"
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define DEMO_KERNEL_PROFILE_VERSION 2U
#define DEMO_KERNEL_PACKET_FRAME_MAX 23U
#define DEMO_KERNEL_WIRE_FRAME_MAX 23U
#define DEMO_KERNEL_REQUEST_FRAME_MAX 23U
#define DEMO_KERNEL_REPLY_FRAME_MAX 23U
#define DEMO_KERNEL_MESSAGE_FRAME_MAX 15U
typedef void (*demo_kernel_execute_callback_t)(void *, wl_kernel_result_t, const response_t *);
typedef struct {
  int32_t (*on_rpc_execute)(void *, const request_t *, response_value_t *);
  void (*on_message_event)(void *, const event_t *);
} demo_kernel_handlers_t;
typedef struct {
  wl_kernel_t core;
  demo_kernel_handlers_t handlers;
  void *user;
  union {
    response_value_t execute;
  } reply;
  uint8_t requests[WL_KERNEL_CALLS][23U];
  uint8_t replies[WL_KERNEL_REPLIES][23U];
  uint8_t message[15U];
} demo_kernel_t;
wl_err_t demo_kernel_init(demo_kernel_t *, wl_packet_port_t *, const demo_kernel_handlers_t *, wl_kernel_error_fn, void *);
wl_err_t demo_kernel_step(demo_kernel_t *, uint32_t now);

wl_err_t demo_kernel_execute_start(demo_kernel_t *, const request_value_t *, uint32_t now, uint32_t timeout, demo_kernel_execute_callback_t, void *, wl_kernel_call_handle_t *);
wl_err_t demo_kernel_send_event(demo_kernel_t *, const event_value_t *);
/* Borrowed input is encoded before return; no view or source bytes are retained. */
wl_err_t demo_kernel_send_event_view(demo_kernel_t *, const event_t *);
#ifdef __cplusplus
}
#endif
#endif

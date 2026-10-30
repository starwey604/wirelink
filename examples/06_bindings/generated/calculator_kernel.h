/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CALCULATOR_KERNEL_H
#define CALCULATOR_KERNEL_H
#include "calculator.h"
#include "wirelink/kernel.h"
#if WL_KERNEL_CONTRACT != 1
#error "static kernel contract mismatch"
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define CALCULATOR_KERNEL_PROFILE_VERSION 2U
#define CALCULATOR_KERNEL_PACKET_FRAME_MAX 29U
#define CALCULATOR_KERNEL_WIRE_FRAME_MAX 29U
#define CALCULATOR_KERNEL_REQUEST_FRAME_MAX 29U
#define CALCULATOR_KERNEL_REPLY_FRAME_MAX 23U
#define CALCULATOR_KERNEL_MESSAGE_FRAME_MAX 4U
typedef void (*calculator_kernel_add_callback_t)(void *, wl_kernel_result_t, const add_response_t *);
typedef struct {
  int32_t (*on_rpc_add)(void *, const add_request_t *, add_response_value_t *);
} calculator_kernel_handlers_t;
typedef struct {
  wl_kernel_t core;
  calculator_kernel_handlers_t handlers;
  void *user;
  union {
    add_response_value_t add;
  } reply;
  uint8_t requests[WL_KERNEL_CALLS][29U];
  uint8_t replies[WL_KERNEL_REPLIES][23U];
  uint8_t message[4U];
} calculator_kernel_t;
wl_err_t calculator_kernel_init(calculator_kernel_t *, wl_packet_port_t *, const calculator_kernel_handlers_t *, wl_kernel_error_fn, void *);
wl_err_t calculator_kernel_step(calculator_kernel_t *, uint32_t now);

wl_err_t calculator_kernel_add_start(calculator_kernel_t *, const add_request_value_t *, uint32_t now, uint32_t timeout, calculator_kernel_add_callback_t, void *, wl_kernel_call_handle_t *);
#ifdef __cplusplus
}
#endif
#endif

/* SPDX-License-Identifier: Apache-2.0 */
#ifndef DEVICE_KERNEL_H
#define DEVICE_KERNEL_H
#include "device.h"
#include "wirelink/kernel.h"
#if WL_KERNEL_CONTRACT != 1
#error "static kernel contract mismatch"
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define DEVICE_KERNEL_PROFILE_VERSION 2U
#define DEVICE_KERNEL_PACKET_FRAME_MAX 233U
#define DEVICE_KERNEL_WIRE_FRAME_MAX 233U
#define DEVICE_KERNEL_REQUEST_FRAME_MAX 227U
#define DEVICE_KERNEL_REPLY_FRAME_MAX 233U
#define DEVICE_KERNEL_MESSAGE_FRAME_MAX 4U
typedef void (*device_kernel_configure_callback_t)(void *, wl_kernel_result_t, const configure_response_t *);
typedef void (*device_kernel_get_info_callback_t)(void *, wl_kernel_result_t, const info_response_t *);
typedef struct {
  int32_t (*on_rpc_configure)(void *, const configure_request_t *, configure_response_value_t *);
  int32_t (*on_rpc_get_info)(void *, const info_request_t *, info_response_value_t *);
} device_kernel_handlers_t;
typedef struct {
  wl_kernel_t core;
  device_kernel_handlers_t handlers;
  void *user;
  union {
    configure_response_value_t configure;
    info_response_value_t get_info;
  } reply;
  uint8_t requests[WL_KERNEL_CALLS][227U];
  uint8_t replies[WL_KERNEL_REPLIES][233U];
  uint8_t message[4U];
} device_kernel_t;
wl_err_t device_kernel_init(device_kernel_t *, wl_packet_port_t *, const device_kernel_handlers_t *, wl_kernel_error_fn, void *);
wl_err_t device_kernel_step(device_kernel_t *, uint32_t now);

wl_err_t device_kernel_configure_start(device_kernel_t *, const configure_request_value_t *, uint32_t now, uint32_t timeout, device_kernel_configure_callback_t, void *, wl_kernel_call_handle_t *);
wl_err_t device_kernel_get_info_start(device_kernel_t *, const info_request_value_t *, uint32_t now, uint32_t timeout, device_kernel_get_info_callback_t, void *, wl_kernel_call_handle_t *);
#ifdef __cplusplus
}
#endif
#endif

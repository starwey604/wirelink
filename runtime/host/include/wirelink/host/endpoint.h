/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_HOST_ENDPOINT_H_
#define WIRELINK_HOST_ENDPOINT_H_
#include "wirelink/kernel.h"
/* Passive outer-runtime descriptor. C prepares/initializes the port storage;
 * C++ never assumes the representation of its C11 atomics. No RPC state here. */
typedef struct {
  void *context;
  wl_kernel_t *core;
  wl_packet_port_t *port;
  size_t maximum_rx_frame;
} wl_host_endpoint_t;
typedef void (*wl_host_rpc_notify_fn)(void *, wl_kernel_result_t);
#endif

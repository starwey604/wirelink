/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_EXAMPLE_PACKET_TOOL_OWNER_H
#define WIRELINK_EXAMPLE_PACKET_TOOL_OWNER_H
#include "wirelink/byte_fifo.h"
#include "wirelink/kernel.h"
#ifdef __cplusplus
extern "C" {
#endif
#define EXAMPLE_PACKET_TOOL_PAYLOAD 2048U
#define EXAMPLE_PACKET_TOOL_FRAME 2052U
/* One active single-owner tool per process. C owns every atomic object. */
wl_err_t example_packet_tool_prepare(uint64_t instance, int serial,
  wl_packet_sink_fn submit, wl_packet_notify_fn notify, void *user);
wl_packet_port_t *example_packet_tool_port(void);
wl_packet_stream_t *example_packet_tool_stream(void);
wl_byte_fifo_t *example_packet_tool_fifo(void);
wl_err_t example_packet_tool_send(uint16_t id, const uint8_t *, size_t);
wl_err_t example_packet_tool_step(uint32_t now);
int example_packet_tool_ready(uint32_t now);
int example_packet_tool_sent(void); /* Real driver admission AND physical drain. */
wl_err_t example_packet_tool_reply(uint16_t *, uint8_t *, size_t, size_t *);
wl_err_t example_packet_tool_close_begin(void);
/* Caller must stop/join actual I/O first. Never synthesizes completion. */
wl_err_t example_packet_tool_close_finish(uint32_t now);
#ifdef __cplusplus
}
#endif
#endif

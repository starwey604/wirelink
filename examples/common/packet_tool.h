/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_EXAMPLE_PACKET_TOOL_H
#define WIRELINK_EXAMPLE_PACKET_TOOL_H
#include "wirelink/node.h"
#ifdef __cplusplus
extern "C" {
#endif
#define EXAMPLE_PACKET_TOOL_PAYLOAD 2048U
#define EXAMPLE_PACKET_TOOL_FRAME 2052U
wl_err_t example_packet_tool_bind(uint32_t instance, int serial, wl_transport_t *transport);
wl_node_t *example_packet_tool_node(void);
wl_err_t example_packet_tool_send(uint16_t id, const uint8_t *, size_t);
wl_err_t example_packet_tool_step(uint32_t now);
int example_packet_tool_sent(void); /* Real driver admission AND physical drain. */
wl_err_t example_packet_tool_reply(uint16_t *, uint8_t *, size_t, size_t *);
wl_err_t example_packet_tool_close(uint32_t now);
#ifdef __cplusplus
}
#endif
#endif

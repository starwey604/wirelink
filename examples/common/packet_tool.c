/* SPDX-License-Identifier: Apache-2.0 */
#include "packet_tool.h"
#include <stdbool.h>
#include <string.h>

/* A tool sends one raw message at a time. Its reply is owned until read. */
static struct {
  wl_node_t node;
  wl_packet_stream_t stream;
  uint8_t rx[2][2112], tx[EXAMPLE_PACKET_TOOL_FRAME], wire[2112];
  uint8_t reply[EXAMPLE_PACKET_TOOL_PAYLOAD];
  size_t reply_length;
  uint16_t reply_id;
  uint32_t previous;
  wl_err_t error;
  bool prepared, has_command, have_reply;
} owner;
static wl_err_t route(wl_endpoint_t *core, const wl_wire_frame_t *frame) {
  (void)core;
  if (frame->length > sizeof(owner.reply)) return WL_ERR_PAYLOAD_TOO_LONG;
  memcpy(owner.reply, frame->payload, frame->length);
  owner.reply_length = frame->length; owner.reply_id = frame->message_id; owner.have_reply = true;
  return WL_OK;
}
wl_err_t example_packet_tool_bind(uint32_t instance, int serial, wl_transport_t *transport) {
  if (!instance || instance == owner.previous) return WL_ERR_INVALID_ARG;
  if (owner.prepared && !wl_node_quiescent(&owner.node)) return WL_ERR_INVALID_STATE;
  static const wl_node_stream_ops_t cobs = {
    wl_packet_stream_cobs_crc16_init, wl_packet_stream_cobs_crc16_submit,
    wl_packet_stream_cobs_crc16_feed, wl_packet_stream_abort_rx, wl_packet_stream_notify,
    WL_NODE_COBS_CRC16
  };
  const size_t bound = serial ? wl_packet_stream_cobs_crc16_bound(sizeof(owner.tx)) : sizeof(owner.tx);
  wl_node_layout_t layout = {
    .instance = instance, .storage = {.message = owner.tx, .message_capacity = sizeof(owner.tx)},
    .route = route, .rx_storage = &owner.rx[0][0], .rx_storage_size = sizeof(owner.rx),
    .rx_frame_max = sizeof(owner.tx), .tx_frame_max = sizeof(owner.tx),
    .rx_wire_max = (uint16_t)bound, .tx_wire_max = (uint16_t)bound, .has_tx = 1,
    .stream = serial ? &owner.stream : NULL, .stream_ops = serial ? &cobs : NULL,
    .stream_tx_wire = {owner.wire, sizeof(owner.wire)}
  };
  owner.prepared = false;
  wl_err_t error = wl_node_bind_transport(&owner.node, &layout, transport);
  if (error == WL_OK) {
    owner.previous = instance;
    owner.has_command = false;
    owner.have_reply = false;
    owner.prepared = true;
    owner.error = WL_OK;
  }
  return error;
}
wl_node_t *example_packet_tool_node(void) { return &owner.node; }
wl_err_t example_packet_tool_send(uint16_t id, const uint8_t *data, size_t length) {
  if (!owner.prepared || owner.node.core.closed || (!data && length) || length > EXAMPLE_PACKET_TOOL_PAYLOAD)
    return WL_ERR_INVALID_ARG;
  if (owner.has_command && !example_packet_tool_sent()) return WL_ERR_WOULD_BLOCK;
  /* Ordinary commands are serialized by this application, not a framework
   * reliability service. Admission snapshots the borrowed bytes immediately. */
  uint8_t *frame;
  size_t capacity = 0U;
  wl_err_t error = wl_endpoint_msg_claim(&owner.node.core, &frame, &capacity);
  if (error == WL_OK) {
    if (capacity < WL_WIRE_NATIVE_HEADER + length) return WL_ERR_BUF_TOO_SMALL;
    if (length) memcpy(frame + WL_WIRE_NATIVE_HEADER, data, length);
    error = wl_wire_frame_seal(frame, capacity, WL_WIRE_MSG, id, length);
    if (error == WL_OK)
      error = wl_endpoint_msg_commit(&owner.node.core, WL_WIRE_NATIVE_HEADER + length);
  }
  if (error == WL_OK) { owner.has_command = true; owner.error = WL_OK; }
  return error;
}
wl_err_t example_packet_tool_step(uint32_t now) {
  if (!owner.prepared || owner.node.core.closed) return WL_ERR_INVALID_STATE;
  /* One RX per outer pass: the caller consumes the owned reply before the
   * next pass, so late application Status messages cannot overwrite a match. */
  wl_transport_t *transport = owner.node.transport;
  wl_err_t error = WL_OK;
  if (transport->ops->service) error = transport->ops->service(transport, now);
  if (error == WL_OK) error = wl_endpoint_step(&owner.node.core, now, 1);
  if (error != WL_OK) owner.error = error;
  return error;
}
int example_packet_tool_sent(void) {
  return owner.prepared && !owner.node.core.closed && owner.has_command && owner.error == WL_OK &&
      !owner.node.core.tx_count && !wl_packet_tx_active(&owner.node.port);
}
wl_err_t example_packet_tool_reply(uint16_t *id, uint8_t *data, size_t capacity, size_t *length) {
  if (!id || !data || !length) return WL_ERR_INVALID_ARG;
  if (!owner.have_reply) return WL_ERR_NO_DATA;
  if (capacity < owner.reply_length) return WL_ERR_BUF_TOO_SMALL;
  memcpy(data, owner.reply, owner.reply_length); *length = owner.reply_length; *id = owner.reply_id;
  owner.have_reply = false; return WL_OK;
}
wl_err_t example_packet_tool_close(uint32_t now) {
  if (!owner.prepared) return WL_OK;
  const bool cancelling = wl_packet_tx_active(&owner.node.port) != 0;
  wl_err_t error = wl_node_close(&owner.node, now);
  if (cancelling && error == WL_ERR_IO && wl_node_quiescent(&owner.node)) return WL_OK;
  return error;
}

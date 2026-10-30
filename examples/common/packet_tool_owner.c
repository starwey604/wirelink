/* SPDX-License-Identifier: Apache-2.0 */
#include "packet_tool_owner.h"
#include <stdbool.h>
#include <string.h>
static struct {
  wl_kernel_t core; wl_packet_port_t port;
  wl_packet_stream_t stream; wl_byte_fifo_t fifo;
  uint8_t rx[2][2112], tx[9][EXAMPLE_PACKET_TOOL_FRAME], wire[2112], bytes[1024];
  uint8_t reply[EXAMPLE_PACKET_TOOL_PAYLOAD];
  size_t reply_length; uint16_t reply_id;
  wl_packet_sink_fn physical_submit; wl_packet_notify_fn notify; void *user;
  uint64_t previous;
  wl_err_t error;
  bool prepared, closed, has_command, accepted, have_reply, cancel_expected;
} owner;
static void notify(void *user) {
  (void)user; if (owner.notify) owner.notify(owner.user);
}
static wl_packet_submit_t submit(void *user, const uint8_t *data, size_t length, wl_packet_token_t token) {
  (void)user;
  wl_packet_submit_t result = owner.physical_submit(owner.user, data, length, token);
  if (result == WL_PACKET_SENT || result == WL_PACKET_STARTED) owner.accepted = true;
  return result;
}
static wl_err_t route(wl_kernel_t *core, const wl_kernel_frame_t *frame) {
  (void)core;
  if (frame->length > sizeof(owner.reply)) return WL_ERR_PAYLOAD_TOO_LONG;
  memcpy(owner.reply, frame->payload, frame->length);
  owner.reply_length = frame->length; owner.reply_id = frame->message_id; owner.have_reply = true;
  return WL_OK;
}
wl_err_t example_packet_tool_prepare(uint64_t instance, int serial,
  wl_packet_sink_fn physical, wl_packet_notify_fn wake, void *user) {
  if (!instance || !physical || instance == owner.previous) return WL_ERR_INVALID_ARG;
  if (owner.prepared && !wl_kernel_quiescent(&owner.core)) return WL_ERR_INVALID_STATE;
  const uint64_t previous = owner.previous;
  memset(&owner, 0, sizeof(owner)); owner.previous = previous;
  owner.physical_submit = physical; owner.notify = wake; owner.user = user;
  wl_packet_config_t config = {.instance = instance, .rx_storage = &owner.rx[0][0],
    .rx_unit_size = sizeof(owner.rx[0]), .rx_storage_size = sizeof(owner.rx),
    .submit = serial ? wl_packet_stream_cobs_crc16_submit : submit,
    .notify = serial ? wl_packet_stream_notify : notify,
    .user = serial ? (void *)&owner.stream : NULL};
  wl_err_t error = wl_packet_init(&owner.port, &config);
  if (error != WL_OK) return error;
  if (serial) {
    wl_packet_stream_config_t stream = {.port = &owner.port, .tx_storage = {owner.wire, sizeof(owner.wire)},
      .maximum_frame_size = EXAMPLE_PACKET_TOOL_FRAME, .physical_submit = submit, .notify = notify};
    error = wl_packet_stream_cobs_crc16_init(&owner.stream, &stream);
    if (error != WL_OK) return error;
    error = wl_byte_fifo_init(&owner.fifo, (wl_span_t){owner.bytes, sizeof(owner.bytes)}, notify, NULL);
    if (error != WL_OK) return error;
  }
  wl_kernel_config_t kernel = {.port = &owner.port, .route = route};
  for (unsigned i = 0; i < 9; ++i) kernel.tx_storage[i] = (wl_span_t){owner.tx[i], sizeof(owner.tx[i])};
  error = wl_kernel_init(&owner.core, &kernel);
  if (error == WL_OK) { owner.previous = instance; owner.prepared = true; }
  return error;
}
wl_packet_port_t *example_packet_tool_port(void) { return &owner.port; }
wl_packet_stream_t *example_packet_tool_stream(void) { return &owner.stream; }
wl_byte_fifo_t *example_packet_tool_fifo(void) { return &owner.fifo; }
typedef struct { const uint8_t *data; size_t size; } input_t;
static wl_err_t encode(const void *user, uint8_t *out, size_t capacity, size_t *length) {
  const input_t *input = user;
  if (input->size > capacity) return WL_ERR_BUF_TOO_SMALL;
  if (input->size) memcpy(out, input->data, input->size);
  *length = input->size; return WL_OK;
}
wl_err_t example_packet_tool_send(uint16_t id, const uint8_t *data, size_t length) {
  if (!owner.prepared || owner.closed || (!data && length) || length > EXAMPLE_PACKET_TOOL_PAYLOAD)
    return WL_ERR_INVALID_ARG;
  if (owner.has_command && !example_packet_tool_sent()) return WL_ERR_WOULD_BLOCK;
  /* Ordinary commands are serialized by this application, not a framework
   * reliability service. Admission snapshots the borrowed bytes immediately. */
  input_t input = {data, length};
  wl_err_t error = wl_kernel_send(&owner.core, id, &input, encode);
  if (error == WL_OK) { owner.has_command = true; owner.accepted = false; owner.error = WL_OK; }
  return error;
}
wl_err_t example_packet_tool_step(uint32_t now) {
  if (!owner.prepared || owner.closed) return WL_ERR_INVALID_STATE;
  /* One RX per outer pass: the caller consumes the owned reply before the
   * next pass, so late application Status messages cannot overwrite a match. */
  wl_err_t error = wl_kernel_step(&owner.core, now, 1);
  if (error != WL_OK) owner.error = error;
  return error;
}
int example_packet_tool_ready(uint32_t now) {
  if (!owner.prepared) return 0;
  int have_deadline = 0; uint32_t deadline = 0;
  return wl_kernel_ready(&owner.core, now, &have_deadline, &deadline);
}
int example_packet_tool_sent(void) {
  return owner.prepared && !owner.closed && owner.accepted && owner.error == WL_OK &&
    !wl_packet_tx_active(&owner.port);
}
wl_err_t example_packet_tool_reply(uint16_t *id, uint8_t *data, size_t capacity, size_t *length) {
  if (!id || !data || !length) return WL_ERR_INVALID_ARG;
  if (!owner.have_reply) return WL_ERR_NO_DATA;
  if (capacity < owner.reply_length) return WL_ERR_BUF_TOO_SMALL;
  memcpy(data, owner.reply, owner.reply_length); *length = owner.reply_length; *id = owner.reply_id;
  owner.have_reply = false; return WL_OK;
}
wl_err_t example_packet_tool_close_begin(void) {
  if (!owner.prepared || owner.closed) return WL_OK;
  owner.cancel_expected = wl_packet_tx_active(&owner.port) != 0;
  wl_err_t error = wl_kernel_close(&owner.core);
  if (error == WL_OK) owner.closed = true;
  return error;
}
wl_err_t example_packet_tool_close_finish(uint32_t now) {
  if (!owner.prepared) return WL_OK;
  if (!owner.closed) return WL_ERR_INVALID_STATE;
  const wl_err_t error = wl_kernel_step(&owner.core, now, 16);
  if (error != WL_OK && !(owner.cancel_expected && error == WL_ERR_IO)) return error;
  return wl_kernel_quiescent(&owner.core) ? WL_OK : WL_ERR_WOULD_BLOCK;
}

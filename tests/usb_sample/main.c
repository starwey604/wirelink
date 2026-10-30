/* SPDX-License-Identifier: Apache-2.0 */
#include "application.h"
#include "wirelink/crc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (0)
static usb_sample_application_t app;
static wl_kernel_t core;
static wl_packet_port_t port;
static uint8_t rx[2][USB_SAMPLE_FRAME_MAX + 1], snapshots[9][USB_SAMPLE_FRAME_MAX];
static const uint8_t *leased;
static size_t lease_length;
static wl_packet_token_t token;
static bool busy;
static unsigned attempts, outputs;
static uint16_t output_ids[64];
static uint32_t tick;
static uint8_t captured[USB_SAMPLE_FRAME_MAX];
static size_t captured_length;
static wl_packet_submit_t send(void *user, const uint8_t *data, size_t length, wl_packet_token_t value) {
  (void)user; ++attempts;
  if (busy) return WL_PACKET_BUSY;
  CHECK(!leased); leased = data; lease_length = length; token = value;
  return WL_PACKET_STARTED;
}
static void pass(void) { CHECK(usb_sample_pass(&app, &core, tick) == WL_OK); }
static void complete(void) {
  CHECK(leased);
  /* The model driver consumes the source, then acknowledges actual drain. */
  memcpy(captured, leased, lease_length); captured_length = lease_length;
  CHECK(wl_packet_tx_publish(&port, token, WL_PACKET_CONSUMED) == WL_OK);
  wl_kernel_frame_t frame;
  CHECK(wl_kernel_native_none_read(captured, captured_length, &frame) == WL_OK);
  CHECK(outputs < 64); output_ids[outputs++] = frame.message_id;
  leased = NULL;
  pass(); CHECK(wl_packet_tx_active(&port));
  CHECK(wl_packet_tx_publish(&port, token, WL_PACKET_DRAINED) == WL_OK);
  pass();
}
static void settle(void) {
  for (unsigned i = 0; i < 40; ++i) { pass(); if (leased) complete(); }
  CHECK(!leased && !app.output_pending && !wl_packet_tx_active(&port));
}
static wl_err_t input(uint16_t id, const uint8_t *body, size_t length) {
  wl_packet_claim_t claim;
  wl_err_t error = wl_packet_rx_claim(&port, length + 4, &claim);
  if (error != WL_OK) return error;
  memcpy(claim.span.data + 4, body, length);
  CHECK(wl_kernel_native_none_seal(claim.span.data, claim.span.length, id, length) == WL_OK);
  return wl_packet_rx_commit(&port, &claim, length + 4);
}
static uint8_t body[USB_SAMPLE_PAYLOAD_MAX];
static size_t length;
static void begin(uint32_t id, uint64_t total, uint32_t crc, uint32_t chunk) {
  bulk_begin_t value = {.has_transfer_id = true, .transfer_id = id, .has_total_length = true,
    .total_length = total, .has_object_crc32c = true, .object_crc32c = crc,
    .has_requested_chunk_size = true, .requested_chunk_size = chunk};
  CHECK(bulk_begin_encode(&value, body, sizeof(body), &length) == WL_CODEC_OK);
  CHECK(input(BULK_BEGIN_MESSAGE_ID, body, length) == WL_OK); settle();
}
static void chunk(uint32_t id, uint64_t offset, const uint8_t *data, size_t size) {
  bulk_chunk_t value = {.has_transfer_id = true, .transfer_id = id, .has_offset = true,
    .offset = offset, .has_data = true, .data = {data, size}};
  CHECK(bulk_chunk_encode(&value, body, sizeof(body), &length) == WL_CODEC_OK);
  CHECK(input(BULK_CHUNK_MESSAGE_ID, body, length) == WL_OK); settle();
}
static void end(uint32_t id, uint64_t total, uint32_t crc) {
  bulk_end_t value = {.has_transfer_id = true, .transfer_id = id, .has_total_length = true,
    .total_length = total, .has_object_crc32c = true, .object_crc32c = crc};
  CHECK(bulk_end_encode(&value, body, sizeof(body), &length) == WL_CODEC_OK);
  CHECK(input(BULK_END_MESSAGE_ID, body, length) == WL_OK); settle();
}
static void status(int code, uint64_t next) {
  wl_kernel_frame_t frame;
  bulk_status_t value;
  CHECK(wl_kernel_native_none_read(captured, captured_length, &frame) == WL_OK);
  CHECK(frame.message_id == BULK_STATUS_MESSAGE_ID);
  CHECK(bulk_status_decode(frame.payload, frame.length, &value) == WL_CODEC_OK);
  CHECK(value.has_code && value.code == code && value.has_next_offset && value.next_offset == next);
}
int main(void) {
  wl_packet_config_t packet = {.instance = 1234, .rx_storage = &rx[0][0],
    .rx_unit_size = sizeof(rx[0]), .rx_storage_size = sizeof(rx), .submit = send};
  CHECK(wl_packet_init(&port, &packet) == WL_OK);
  wl_kernel_config_t config = {.port = &port, .route = usb_sample_route, .user = &app};
  for (unsigned i = 0; i < 9; ++i) config.tx_storage[i] = (wl_span_t){snapshots[i], sizeof(snapshots[i])};
  CHECK(wl_kernel_init(&core, &config) == WL_OK);
  /* Full-size opaque echo is independent of the unbounded/public schema. */
  memset(body, 0x7e, sizeof(body));
  CHECK(input(70, body, sizeof(body)) == WL_OK); memset(body, 0, sizeof(body)); settle();
  CHECK(captured_length == sizeof(body) + 4);
  for (size_t i = 4; i < captured_length; ++i) CHECK(captured[i] == 0x7e);
  /* One ordinary snapshot plus one application output: BUSY cannot spin or
   * consume a third command. The remaining two input units apply pressure. */
  busy = true;
  CHECK(input(71, body, 8) == WL_OK);
  for (unsigned i = 0; i < 5; ++i) pass();
  CHECK(input(72, body, 8) == WL_OK);
  for (unsigned i = 0; i < 5; ++i) pass();
  CHECK(app.output_pending);
  CHECK(input(73, body, 8) == WL_OK && input(74, body, 8) == WL_OK);
  CHECK(input(75, body, 8) == WL_ERR_WOULD_BLOCK);
  for (unsigned i = 0; i < 5; ++i) pass();
  unsigned before = attempts;
  for (unsigned i = 0; i < 1000; ++i) {
    CHECK(!usb_sample_ready(&app, &core, tick)); pass();
  }
  CHECK(attempts == before);
  busy = false; wl_packet_activity(&port); settle();
  CHECK(outputs == 5);
  for (unsigned i = 0; i < outputs; ++i) CHECK(output_ids[i] == 70 + i);
  uint8_t pattern[512];
  for (size_t i = 0; i < sizeof(pattern); ++i) pattern[i] = (uint8_t)i ^ 0xa5;
  const uint32_t crc = wl_crc32c(pattern, sizeof(pattern));
  tick = UINT32_MAX - 500;
  begin(1, sizeof(pattern), crc, 256); status(CONTROL_BULK_STATUS_OK, 0);
  CHECK(app.chunk_size == 256);
  begin(2, sizeof(pattern), crc, 256); status(CONTROL_BULK_STATUS_BUSY, 0);
  chunk(1, 256, pattern + 256, 256); status(CONTROL_BULK_STATUS_OUT_OF_ORDER, 0);
  chunk(1, 0, pattern, 256); status(CONTROL_BULK_STATUS_OK, 256);
  chunk(1, 0, pattern, 256); status(CONTROL_BULK_STATUS_OK, 256);
  CHECK(app.writes == 1 && app.duplicates == 1);
  chunk(1, 256, pattern + 256, 256); status(CONTROL_BULK_STATUS_OK, 512);
  end(1, 512, crc); status(CONTROL_BULK_STATUS_OK, 512);
  end(1, 512, crc); status(CONTROL_BULK_STATUS_OK, 512);
  CHECK(app.commits == 1 && app.completed_writes == 2);
  begin(1, 512, crc ^ 1, 256); status(CONTROL_BULK_STATUS_CONFLICT, 512);
  begin(3, 512, crc, 256); tick += 1001; settle();
  status(CONTROL_BULK_STATUS_TIMED_OUT, 0); CHECK(!app.active && app.aborts == 1);
  begin(4, 512, crc, 256);
  pattern[0] ^= 1;
  chunk(4, 0, pattern, 256); status(CONTROL_BULK_STATUS_INTEGRITY_FAILED, 0);
  CHECK(app.writes == 0 && app.next == 0);
  usb_sample_close(&app); CHECK(app.aborts == 2 && !app.active);
  CHECK(wl_kernel_close(&core) == WL_OK); pass(); CHECK(wl_kernel_quiescent(&core));
  puts("USB application raw echo/pressure/no-spin/CRC/duplicates/commit/timeout/close PASS");
  return 0;
}

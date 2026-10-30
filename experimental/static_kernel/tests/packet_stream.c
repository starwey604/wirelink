/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/packet_stream.h"
#include "wirelink/kernel_wire.h"
#include "wirelink/frame.h"
#include <stdlib.h>
#include <string.h>
#ifdef WL_PACKET_ZTEST
#include <zephyr/ztest.h>
#define CHECK(x) zassert_true(x, "%s", #x)
#else
#define CHECK(x) do { if (!(x)) abort(); } while (0)
#endif
typedef struct {
  wl_packet_port_t port;
  wl_packet_stream_t stream;
  uint8_t rx[2][2200], tx[2200], sent[2200];
  size_t sent_length;
} fixture_t;
static wl_packet_submit_t physical(void *user, const uint8_t *data, size_t length, wl_packet_token_t token) {
  fixture_t *f = user; (void)token;
  CHECK(length <= sizeof(f->sent)); memcpy(f->sent, data, length); f->sent_length = length;
  return WL_PACKET_SENT;
}
static void init(fixture_t *f, int cobs) {
  wl_packet_config_t p = {.instance = 77, .rx_storage = &f->rx[0][0],
      .rx_unit_size = 2200, .rx_storage_size = sizeof(f->rx),
      .submit = cobs ? wl_packet_stream_cobs_crc16_submit : wl_packet_stream_length16_none_submit,
      .notify = wl_packet_stream_notify, .user = &f->stream};
  CHECK(wl_packet_init(&f->port, &p) == WL_OK);
  wl_packet_stream_config_t c = {.port = &f->port, .tx_storage = {f->tx, sizeof(f->tx)},
      .physical_submit = physical, .user = f, .maximum_frame_size = 2052};
  CHECK((cobs ? wl_packet_stream_cobs_crc16_init : wl_packet_stream_length16_none_init)(&f->stream, &c) == WL_OK);
}
void wl_test_packet_stream(int cobs) {
  fixture_t f = {0}; init(&f, cobs);
  wl_err_t (*feed)(wl_packet_stream_t *, const uint8_t *, size_t, size_t *) =
      cobs ? wl_packet_stream_cobs_crc16_feed : wl_packet_stream_length16_none_feed;
  uint8_t canonical[2052], expected[2200];
  for (size_t size = 0; size <= 2048; ++size) {
    for (size_t i = 0; i < size; ++i) canonical[4 + i] = (uint8_t)(i + size);
    CHECK(wl_kernel_native_none_seal(canonical, sizeof(canonical), 123, size) == WL_OK);
    wl_packet_token_t token;
    CHECK(wl_packet_tx_submit(&f.port, canonical, size + 4, &token) == WL_OK);
    CHECK(wl_packet_tx_take(&f.port, &token) == (WL_PACKET_CONSUMED | WL_PACKET_DRAINED));
    wl_wire_packet_t packet = {.type = WL_PACKET_DATA, .message_id = 123,
        .integrity = cobs ? WL_INTEGRITY_CRC16 : WL_INTEGRITY_NONE,
        .payload = canonical + 4, .payload_len = size};
    size_t n = 0;
    CHECK(wl_frame_encode(&packet, cobs ? WL_ENVELOPE_COBS_STREAM : WL_ENVELOPE_BUS_LENGTH16,
        expected, sizeof(expected), &n) == WL_OK);
    CHECK(n == f.sent_length && !memcmp(expected, f.sent, n));
    for (size_t at = 0; at < n;) {
      size_t chunk = (size + at) % 17 + 1, consumed = 0;
      if (chunk > n - at) chunk = n - at;
      CHECK(feed(&f.stream, f.sent + at, chunk, &consumed) == WL_OK && consumed == chunk);
      at += consumed;
    }
    wl_packet_borrow_t borrow;
    CHECK(wl_packet_rx_borrow(&f.port, &borrow) == WL_OK && borrow.length == size + 4);
    CHECK(!memcmp(borrow.data, canonical, borrow.length));
    CHECK(wl_packet_rx_release(&f.port, &borrow) == WL_OK);
  }
  // Full slots retain the caller's unconsumed suffix; partial prefix remains bounded.
  for (unsigned i = 0; i < 2; ++i) {
    size_t consumed = 0; CHECK(feed(&f.stream, f.sent, f.sent_length, &consumed) == WL_OK);
    CHECK(consumed == f.sent_length);
  }
  size_t consumed = 0;
  CHECK(feed(&f.stream, f.sent, f.sent_length, &consumed) == WL_ERR_WOULD_BLOCK);
  CHECK(consumed == (cobs ? 0U : 2U));
  wl_packet_borrow_t borrow; CHECK(wl_packet_rx_borrow(&f.port, &borrow) == WL_OK);
  CHECK(wl_packet_rx_release(&f.port, &borrow) == WL_OK);
  size_t suffix = 0; CHECK(feed(&f.stream, f.sent + consumed, f.sent_length - consumed, &suffix) == WL_OK);
  CHECK(consumed + suffix == f.sent_length);
  for (unsigned i = 0; i < 2; ++i) {
    CHECK(wl_packet_rx_borrow(&f.port, &borrow) == WL_OK);
    CHECK(wl_packet_rx_release(&f.port, &borrow) == WL_OK);
  }
  if (cobs) {
    memcpy(expected, f.sent, f.sent_length); expected[2] ^= 1;
    CHECK(feed(&f.stream, expected, f.sent_length, &consumed) != WL_OK);
    CHECK(!wl_packet_rx_ready(&f.port));
  }
  CHECK(feed(&f.stream, f.sent, f.sent_length, &consumed) == WL_OK);
  CHECK(wl_packet_rx_borrow(&f.port, &borrow) == WL_OK);
  CHECK(wl_packet_rx_release(&f.port, &borrow) == WL_OK);
  CHECK(feed(&f.stream, f.sent, f.sent_length / 2, &consumed) == WL_OK);
  CHECK(wl_packet_stream_abort_rx(&f.stream) == WL_OK);
  wl_packet_stop(&f.port); CHECK(wl_packet_quiescent(&f.port));
}
#ifndef WL_PACKET_ZTEST
int main(void) { wl_test_packet_stream(1); wl_test_packet_stream(0); }
#endif

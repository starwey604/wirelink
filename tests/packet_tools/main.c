/* SPDX-License-Identifier: Apache-2.0 */
#include "packet_tool_owner.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) abort(); } while (0)
static struct {
  const uint8_t *borrowed;
  uint8_t copy[2112]; size_t length;
  wl_packet_token_t token;
  bool busy, physical;
} model;
static wl_packet_submit_t submit(void *user, const uint8_t *data, size_t length, wl_packet_token_t token) {
  (void)user; CHECK(!model.physical && length <= sizeof(model.copy));
  if (model.busy) return WL_PACKET_BUSY;
  memcpy(model.copy, data, length); model.length = length; model.borrowed = data;
  model.token = token; model.physical = true; return WL_PACKET_STARTED;
}
static void service_rx(int serial) {
  if (serial) {
    size_t offset = 0;
    while (offset < model.length) {
      wl_span_t write;
      CHECK(wl_byte_fifo_reserve(example_packet_tool_fifo(), &write) == WL_OK);
      size_t take = model.length - offset; if (take > write.length) take = write.length;
      memcpy(write.data, model.copy + offset, take);
      CHECK(wl_byte_fifo_commit(example_packet_tool_fifo(), take) == WL_OK); offset += take;
      CHECK(wl_byte_fifo_to_cobs(example_packet_tool_fifo(), example_packet_tool_stream(), 16) == WL_OK);
    }
  } else {
    wl_packet_claim_t claim;
    CHECK(wl_packet_rx_claim(example_packet_tool_port(), model.length, &claim) == WL_OK);
    memcpy(claim.span.data, model.copy, model.length);
    CHECK(wl_packet_rx_commit(example_packet_tool_port(), &claim, model.length) == WL_OK);
  }
}
static void consume(void) {
  CHECK(model.physical && model.borrowed && !memcmp(model.borrowed, model.copy, model.length));
  model.borrowed = NULL;
  CHECK(wl_packet_tx_publish(example_packet_tool_port(), model.token, WL_PACKET_CONSUMED) == WL_OK);
}
static void drain(void) {
  CHECK(model.physical && !model.borrowed); model.physical = false;
  CHECK(wl_packet_tx_publish(example_packet_tool_port(), model.token, WL_PACKET_DRAINED) == WL_OK);
}
static void run(int serial, uint64_t instance) {
  memset(&model, 0, sizeof(model));
  CHECK(example_packet_tool_prepare(instance, serial, submit, NULL, NULL) == WL_OK);
  CHECK(example_packet_tool_prepare(instance + 100, serial, submit, NULL, NULL) == WL_ERR_INVALID_STATE);
  uint8_t data[EXAMPLE_PACKET_TOOL_PAYLOAD], output[EXAMPLE_PACKET_TOOL_PAYLOAD];
  const size_t sizes[] = {4, 60, 124, 512, 2048};
  for (unsigned c = 0; c < sizeof(sizes) / sizeof(sizes[0]); ++c) {
    for (size_t i = 0; i < sizes[c]; ++i) data[i] = (uint8_t)i;
    model.busy = true;
    CHECK(example_packet_tool_send(70, data, sizes[c]) == WL_OK);
    memset(data, 0xdd, sizes[c]);
    CHECK(example_packet_tool_step(0) == WL_OK && !example_packet_tool_sent());
    CHECK(example_packet_tool_send(71, data, sizes[c]) == WL_ERR_WOULD_BLOCK);
    model.busy = false; wl_packet_activity(example_packet_tool_port());
    CHECK(example_packet_tool_step(0) == WL_OK && model.physical);
    consume(); CHECK(example_packet_tool_step(0) == WL_OK && !example_packet_tool_sent());
    /* Actual driver transfer into peer storage/staging precedes DRAINED. */
    service_rx(serial); drain();
    CHECK(example_packet_tool_step(0) == WL_OK && example_packet_tool_sent());
    CHECK(example_packet_tool_step(0) == WL_OK);
    uint16_t id; size_t length;
    CHECK(example_packet_tool_reply(&id, output, sizeof(output), &length) == WL_OK);
    CHECK(id == 70 && length == sizes[c]);
    for (size_t i = 0; i < length; ++i) CHECK(output[i] == (uint8_t)i);
    CHECK(example_packet_tool_reply(&id, output, sizeof(output), &length) == WL_ERR_NO_DATA);
  }
  CHECK(example_packet_tool_send(70, data, 4) == WL_OK);
  CHECK(example_packet_tool_step(0) == WL_OK && model.physical);
  CHECK(example_packet_tool_close_begin() == WL_OK);
  CHECK(example_packet_tool_close_finish(0) == WL_ERR_WOULD_BLOCK);
  CHECK(!memcmp(model.borrowed, model.copy, model.length));
  model.borrowed = NULL; model.physical = false; /* Actual model driver cancellation. */
  CHECK(wl_packet_tx_publish(example_packet_tool_port(), model.token, WL_PACKET_TX_ERROR) == WL_OK);
  if (serial) CHECK(wl_packet_stream_abort_rx(example_packet_tool_stream()) == WL_OK);
  CHECK(example_packet_tool_close_finish(0) == WL_OK);
  CHECK(example_packet_tool_close_begin() == WL_OK && example_packet_tool_close_finish(0) == WL_OK);
  CHECK(example_packet_tool_prepare(instance, serial, submit, NULL, NULL) == WL_ERR_INVALID_ARG);
}
int main(void) { run(0, 1001); run(1, 1002); }

/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/node.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); abort(); } } while (0)
void test_packet_tool(void);

typedef struct {
  wl_transport_t base;
  uint8_t slots[WL_PACKET_RX_SLOTS][64];
  wl_err_t attach_result, stop_result;
  unsigned services, stops, wakes;
  uint32_t waited;
} driver_t;

static wl_err_t configure(wl_transport_t *base, size_t minimum, wl_transport_rx_override_t *out) {
  driver_t *driver = (driver_t *)base;
  CHECK(minimum == 17U);
  *out = (wl_transport_rx_override_t){&driver->slots[0][0], sizeof(driver->slots), 64U};
  return WL_OK;
}

static wl_err_t attach(wl_transport_t *base, const wl_transport_attach_t *info) {
  CHECK(info->framing == WL_NODE_NATIVE && !info->stream && info->rx_wire_max == 16U);
  CHECK(wl_packet_rx_unit_size(info->port) == 64U);
  return ((driver_t *)base)->attach_result;
}

static void notify(wl_transport_t *base) {
  ++((driver_t *)base)->wakes;
}

static wl_err_t fail_after_prefix(wl_transport_t *base, const wl_transport_attach_t *info) {
  (void)base;
  const uint8_t prefix = 1U;
  size_t consumed = 0;
  CHECK(wl_packet_stream_cobs_crc16_feed(info->stream, &prefix, 1U, &consumed) == WL_OK);
  CHECK(consumed == 1U && info->stream->claimed);
  return WL_ERR_IO;
}

static void test_failed_stream_attach(void) {
  static const wl_transport_ops_t ops = {.attach = fail_after_prefix, .notify = notify};
  static const wl_node_stream_ops_t cobs = {
    wl_packet_stream_cobs_crc16_init, wl_packet_stream_cobs_crc16_submit,
    wl_packet_stream_cobs_crc16_feed, wl_packet_stream_abort_rx, wl_packet_stream_notify,
    WL_NODE_COBS_CRC16
  };
  wl_node_t node = {0};
  wl_packet_stream_t stream;
  uint8_t rx[2][32], tx[32];
  wl_node_layout_t layout = {
    .rx_storage = &rx[0][0], .rx_storage_size = sizeof(rx), .rx_wire_max = 20U,
    .tx_wire_max = 20U, .rx_frame_max = 16U, .tx_frame_max = 16U,
    .stream = &stream, .stream_ops = &cobs, .stream_tx_wire = {tx, sizeof(tx)}
  };
  driver_t driver = {.base = {&ops}};
  CHECK(wl_node_bind_transport(&node, &layout, &driver.base) == WL_ERR_IO);
  CHECK(!stream.claimed && wl_packet_quiescent(&node.port) && driver.wakes == 0U);
  CHECK(wl_node_cancel(&node, (wl_call_handle_t){0}) == WL_ERR_INVALID_ARG);
}
static wl_err_t service(wl_transport_t *base, uint32_t now_ms) {
  (void)now_ms;
  ++((driver_t *)base)->services;
  return WL_OK;
}

static wl_io_readiness_t readiness(const wl_transport_t *base, uint32_t now_ms) {
  (void)base; (void)now_ms;
  return wl_io_readiness_at(15U);
}

static wl_err_t wait(wl_transport_t *base, uint32_t maximum) {
  ((driver_t *)base)->waited = maximum;
  return WL_ERR_NO_DATA;
}

static wl_err_t stop(wl_transport_t *base) {
  driver_t *driver = (driver_t *)base;
  ++driver->stops;
  return driver->stop_result;
}

int main(void) {
  static const wl_transport_ops_t ops = {
    .configure_rx = configure, .attach = attach, .service = service, .readiness = readiness,
    .wait = wait, .stop = stop
  };
  driver_t driver = {.base = {&ops}, .attach_result = WL_ERR_IO};
  wl_node_layout_t layout = {.rx_frame_max = 16U, .rx_wire_max = 16U};
  wl_node_t node = {0};
  CHECK(wl_node_bind_transport(&node, &layout, &driver.base) == WL_ERR_IO);
  CHECK(!node.transport && !node.core.port);
  driver.attach_result = WL_OK;
  CHECK(wl_node_bind_transport(&node, &layout, &driver.base) == WL_OK);
  CHECK(wl_node_wait(&node, 100U, 90U) == WL_OK && driver.waited == 15U);
  CHECK(wl_node_wait(&node, 100U, 2U) == WL_OK && driver.waited == 2U);
  size_t consumed = 1;
  CHECK(wl_node_rx_feed(&node, NULL, 0, &consumed) == WL_ERR_INVALID_STATE && !consumed);
  uint8_t data[17] = {0};
  CHECK(wl_node_rx_packet(&node, data, sizeof(data)) == WL_ERR_BUF_TOO_SMALL);
  driver.stop_result = WL_ERR_IO;
  CHECK(wl_node_close(&node, 100U) == WL_ERR_IO && !wl_node_quiescent(&node));
  driver.stop_result = WL_ERR_WOULD_BLOCK;
  CHECK(wl_node_close(&node, 101U) == WL_ERR_WOULD_BLOCK && driver.services == 1U);
  driver.stop_result = WL_OK;
  CHECK(wl_node_close(&node, 102U) == WL_OK && wl_node_quiescent(&node));
  CHECK(wl_node_close(&node, 103U) == WL_OK && driver.stops == 3U);
  test_failed_stream_attach();
  test_packet_tool();
}

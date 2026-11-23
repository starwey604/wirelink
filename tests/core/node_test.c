/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/node.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); abort(); } } while (0)
void test_packet_tool(void);

typedef struct {
  wl_transport_t base;
  uint8_t slots[WL_PACKET_RX_SLOTS][64];
  wl_err_t attach_result, stop_result, wait_result;
  unsigned services, stops, wakes, waits, clock_reads;
  uint32_t waited, now, service_time;
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
  driver_t *driver = (driver_t *)base;
  ++driver->services;
  driver->service_time = now_ms;
  return WL_OK;
}

static wl_io_readiness_t readiness(const wl_transport_t *base, uint32_t now_ms) {
  (void)base;
  return wl_io_readiness_at(now_ms < 115U ? 115U - now_ms : 0U);
}

static wl_err_t driver_wait(wl_transport_t *base, uint32_t maximum) {
  driver_t *driver = (driver_t *)base;
  ++driver->waits;
  driver->waited = maximum;
  if (driver->wait_result == WL_ERR_NO_DATA) driver->now += maximum;
  return driver->wait_result;
}

static wl_err_t stop(wl_transport_t *base) {
  driver_t *driver = (driver_t *)base;
  ++driver->stops;
  return driver->stop_result;
}

static const wl_transport_ops_t ops = {
  .configure_rx = configure, .attach = attach, .service = service, .readiness = readiness,
  .wait = driver_wait, .stop = stop
};

static uint32_t clock_now(void *user) {
  driver_t *driver = user;
  ++driver->clock_reads;
  return driver->now;
}

static void poll_from_callback(void *user, uint16_t id, wl_err_t error) {
  wl_node_t *node = user;
  (void)id;
  CHECK(error == WL_ERR_BAD_FRAME);
  CHECK(wl_node_poll(node, (wl_clock_t){clock_now, node->transport}, 100U) == WL_ERR_REENTRANT);
}

static void test_node_poll(void) {
  driver_t driver = {.base = {&ops}, .now = 100U, .wait_result = WL_ERR_NO_DATA};
  const wl_clock_t clock = {clock_now, &driver};
  wl_node_t node = {0};
  wl_node_layout_t layout = {
    .rx_frame_max = 16U, .rx_wire_max = 16U, .on_error = poll_from_callback, .user = &node
  };
  CHECK(wl_node_poll(NULL, clock, 100U) == WL_ERR_INVALID_ARG);
  CHECK(wl_node_poll(&node, clock, 100U) == WL_ERR_INVALID_ARG);
  CHECK(wl_node_bind_transport(&node, &layout, &driver.base) == WL_OK);
  CHECK(wl_node_poll(&node, (wl_clock_t){0}, 100U) == WL_ERR_INVALID_ARG);
  CHECK(driver.clock_reads == 0U && driver.waits == 0U && driver.services == 0U);

  CHECK(wl_node_poll(&node, clock, 0U) == WL_OK);
  CHECK(driver.clock_reads == 1U && driver.waits == 0U);
  CHECK(driver.services == 1U && driver.service_time == 100U);

  /* An RX packet is already queued. Its error callback runs inside step. */
  const uint8_t bad_frame = 0U;
  CHECK(wl_node_rx_packet(&node, &bad_frame, sizeof(bad_frame)) == WL_OK);
  CHECK(wl_node_poll(&node, clock, 100U) == WL_ERR_BAD_FRAME);
  CHECK(driver.clock_reads == 3U && driver.waits == 0U && driver.services == 2U);

  driver.wait_result = WL_ERR_IO;
  CHECK(wl_node_poll(&node, clock, 100U) == WL_ERR_IO);
  CHECK(driver.waits == 1U && driver.clock_reads == 4U && driver.services == 2U);

  driver.wait_result = WL_ERR_NO_DATA;
  CHECK(wl_node_poll(&node, clock, 100U) == WL_OK);
  CHECK(driver.waits == 2U && driver.waited == 15U && driver.clock_reads == 6U);
  CHECK(driver.services == 3U && driver.service_time == 115U);
  CHECK(wl_node_close(&node, driver.now) == WL_OK);
}

int main(void) {
  driver_t driver = {.base = {&ops}, .attach_result = WL_ERR_IO, .wait_result = WL_ERR_NO_DATA};
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
  test_node_poll();
  test_packet_tool();
}

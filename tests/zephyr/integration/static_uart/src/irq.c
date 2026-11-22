/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/ztest.h>
#include "fixture.h"
#include "wirelink/wire.h"
#include "wirelink/endpoint.h"
#include "wirelink/zephyr/uart_irq.h"

struct fake_uart {
  uart_irq_callback_user_data_t callback;
  void *user_data;
  uint8_t rx[128], tx[128];
  size_t rx_length, rx_offset, tx_length;
  bool rx_enabled, tx_enabled, pending, read_zero, fill_zero;
  int line_idle, error;
  unsigned reads, fills, complete_queries;
  size_t read_chunk;
  size_t fill_chunk;
  int read_error, fill_error;
  bool level_pending;
  void (*on_read)(void);
  void (*on_complete)(void);
};
static struct fake_uart full_data, cdc_data, missing_data, no_callback_data;

static int fake_init(const struct device *device) {
  (void)device;
  return 0;
}
static void fake_callback_set(const struct device *device, uart_irq_callback_user_data_t callback,
                              void *user_data) {
  struct fake_uart *data = device->data;
  data->callback = callback;
  data->user_data = user_data;
}
static void fake_rx_enable(const struct device *device) {
  ((struct fake_uart *)device->data)->rx_enabled = true;
}
static void fake_rx_disable(const struct device *device) {
  ((struct fake_uart *)device->data)->rx_enabled = false;
}
static void fake_tx_enable(const struct device *device) {
  ((struct fake_uart *)device->data)->tx_enabled = true;
}
static void fake_tx_disable(const struct device *device) {
  ((struct fake_uart *)device->data)->tx_enabled = false;
}
static int fake_rx_ready(const struct device *device) {
  struct fake_uart *data = device->data;
  return data->rx_enabled && data->rx_offset != data->rx_length;
}
static int fake_tx_ready(const struct device *device) {
  return ((struct fake_uart *)device->data)->tx_enabled;
}
static int fake_complete(const struct device *device) {
  struct fake_uart *data = device->data;
  ++data->complete_queries;
  if (data->on_complete != NULL)
    data->on_complete();
  return data->line_idle;
}
static int fake_error(const struct device *device) {
  struct fake_uart *data = device->data;
  int error = data->error;
  data->error = 0;
  return error;
}
static void fake_update(const struct device *device) { (void)device; }
static int fake_pending(const struct device *device) {
  struct fake_uart *data = device->data;
  bool pending = data->pending;
  data->pending = false;
  return pending || (data->level_pending && (fake_rx_ready(device) || fake_tx_ready(device)));
}
static int fake_read(const struct device *device, uint8_t *out, int capacity) {
  struct fake_uart *data = device->data;
  ++data->reads;
  if (data->on_read != NULL)
    data->on_read();
  if (data->read_error)
    return data->read_error;
  if (data->read_zero)
    return 0;
  size_t size = MIN((size_t)capacity, data->rx_length - data->rx_offset);
  if (data->read_chunk != 0U)
    size = MIN(size, data->read_chunk);
  memcpy(out, data->rx + data->rx_offset, size);
  data->rx_offset += size;
  return (int)size;
}
static int fake_fill(const struct device *device, const uint8_t *input, int length) {
  struct fake_uart *data = device->data;
  ++data->fills;
  if (data->fill_error)
    return data->fill_error;
  if (data->fill_zero)
    return 0;
  if (data->fill_chunk != 0U)
    length = MIN((size_t)length, data->fill_chunk);
  zassert_true(data->tx_length + (size_t)length <= sizeof(data->tx));
  memcpy(data->tx + data->tx_length, input, (size_t)length);
  data->tx_length += (size_t)length;
  return length;
}
#define COMMON_API                                                                                 \
  .irq_callback_set = fake_callback_set, .irq_rx_enable = fake_rx_enable,                          \
  .irq_rx_disable = fake_rx_disable, .irq_tx_enable = fake_tx_enable,                              \
  .irq_tx_disable = fake_tx_disable, .irq_rx_ready = fake_rx_ready, .irq_tx_ready = fake_tx_ready, \
  .irq_update = fake_update, .irq_is_pending = fake_pending

static DEVICE_API(uart, full_api) = {
    COMMON_API,
    .fifo_read = fake_read,
    .fifo_fill = fake_fill,
    .irq_tx_complete = fake_complete,
    .err_check = fake_error,
};
static DEVICE_API(uart, cdc_api) = {
    COMMON_API,
    .fifo_read = fake_read,
    .fifo_fill = fake_fill,
};
static DEVICE_API(uart, missing_api) = {COMMON_API, .irq_tx_complete = fake_complete};
static DEVICE_API(uart, no_callback_api) = {.irq_update = fake_update};
DEVICE_DEFINE(fake_uart_full, "fake_uart_full", fake_init, NULL, &full_data, NULL, POST_KERNEL,
              CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &full_api);
DEVICE_DEFINE(fake_uart_cdc, "fake_uart_cdc", fake_init, NULL, &cdc_data, NULL, POST_KERNEL,
              CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &cdc_api);
DEVICE_DEFINE(fake_uart_missing, "fake_uart_missing", fake_init, NULL, &missing_data, NULL,
              POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &missing_api);
DEVICE_DEFINE(fake_uart_no_callback, "fake_uart_no_callback", fake_init, NULL, &no_callback_data,
              NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &no_callback_api);

static wl_zephyr_uart_irq_t adapter;
static wl_node_t node;
static wl_packet_stream_t stream;
static uint8_t rx[2][96], tx[96], bytes[16];
static uint32_t instance;
static void setup_interval(uint32_t interval) {
  memset(&full_data, 0, sizeof(full_data));
  full_data.line_idle = 1;
  wl_zephyr_uart_irq_config_t config = {
      .uart = DEVICE_GET(fake_uart_full), .fifo_storage = {bytes, sizeof(bytes)}, .drain_check_ms = interval};
  zassert_ok(bind_uart(&node, &stream, ++instance, (wl_span_t){rx[0], sizeof(rx)},
      (wl_span_t){tx, sizeof(tx)}, wl_zephyr_uart_irq_transport(&adapter, &config)));
}
static void setup(void) { setup_interval(0); }
static void fire(void) {
  full_data.pending = true;
  full_data.callback(DEVICE_GET(fake_uart_full), full_data.user_data);
}
ZTEST(static_uart_irq, test_staged_rx_physical_tx_and_old_callback) {
  setup();
  uint8_t packet[41];
  memset(packet + 4, 55, 37);
  zassert_ok(wl_wire_native_none_seal(packet, sizeof(packet), 22, 37));
  wl_packet_token_t token;
  zassert_ok(wl_packet_tx_submit(&node.port, packet, sizeof(packet), &token));
  full_data.line_idle = 0;
  fire();
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 0));
  zassert_equal(wl_packet_tx_take(&node.port, &token), WL_PACKET_CONSUMED);
  zassert_true(wl_packet_tx_active(&node.port));
  zassert_false(full_data.tx_enabled);
  full_data.line_idle = 1;
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 1));
  zassert_equal(wl_packet_tx_take(&node.port, &token), WL_PACKET_DRAINED);
  memcpy(full_data.rx, full_data.tx, full_data.tx_length);
  full_data.rx_length = full_data.tx_length;
  fire();
  zassert_false(wl_packet_rx_ready(&node.port)); // IRQ staged bytes, no framing/parser.
  for (unsigned i = 0; i < 20 && !wl_packet_rx_ready(&node.port); ++i) {
    zassert_ok(wl_zephyr_uart_irq_service(&adapter, i + 2));
    fire();
  }
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 24));
  wl_packet_borrow_t borrow;
  zassert_ok(wl_packet_rx_borrow(&node.port, &borrow));
  zassert_equal(borrow.length, sizeof(packet));
  zassert_mem_equal(borrow.data, packet, sizeof(packet));
  zassert_ok(wl_packet_rx_release(&node.port, &borrow));
  uart_irq_callback_user_data_t old = full_data.callback;
  void *old_user = full_data.user_data;
  zassert_ok(wl_node_close(&node, 0));
  zassert_true(wl_packet_quiescent(&node.port));
  setup();
  old(DEVICE_GET(fake_uart_full), old_user);
  zassert_false(wl_byte_fifo_readable(&adapter.fifo));
  zassert_ok(wl_node_close(&node, 0));
  zassert_true(wl_packet_quiescent(&node.port));
}
ZTEST(static_uart_irq, test_rejects_cdc_without_physical_completion) {
  setup();
  zassert_ok(wl_node_close(&node, 0));
  wl_zephyr_uart_irq_config_t config = {.uart = DEVICE_GET(fake_uart_cdc),
    .fifo_storage = {bytes, sizeof(bytes)}};
  zassert_equal(bind_uart(&node, &stream, ++instance, (wl_span_t){rx[0], sizeof(rx)},
      (wl_span_t){tx, sizeof(tx)}, wl_zephyr_uart_irq_transport(&adapter, &config)), WL_ERR_NOT_SUPPORTED);
  zassert_is_null(cdc_data.callback);
  config.uart = DEVICE_GET(fake_uart_missing);
  zassert_equal(bind_uart(&node, &stream, ++instance, (wl_span_t){rx[0], sizeof(rx)},
      (wl_span_t){tx, sizeof(tx)}, wl_zephyr_uart_irq_transport(&adapter, &config)), WL_ERR_NOT_SUPPORTED);
  config.uart = DEVICE_GET(fake_uart_no_callback);
  zassert_equal(bind_uart(&node, &stream, ++instance, (wl_span_t){rx[0], sizeof(rx)},
      (wl_span_t){tx, sizeof(tx)}, wl_zephyr_uart_irq_transport(&adapter, &config)), WL_ERR_NOT_SUPPORTED);
  /* Failed admission cannot consume a binding or prevent a later real UART. */
  setup();
  zassert_ok(wl_node_close(&node, 0));
}
static uint8_t short_packet[8];
static wl_packet_token_t pending_tx(uint32_t now) {
  memset(short_packet, 0, sizeof(short_packet));
  zassert_ok(wl_wire_native_none_seal(short_packet, sizeof(short_packet), 22, 4));
  wl_packet_token_t token;
  zassert_ok(wl_packet_tx_submit(&node.port, short_packet, sizeof(short_packet), &token));
  full_data.line_idle = 0;
  fire();
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, now));
  zassert_equal(wl_packet_tx_take(&node.port, &token), WL_PACKET_CONSUMED);
  zassert_equal(full_data.complete_queries, 1);
  return token;
}
static void receive_byte(uint8_t byte) {
  zassert_true(full_data.rx_length < sizeof(full_data.rx));
  full_data.rx[full_data.rx_length++] = byte;
  fire();
}
static void finish_pending(wl_packet_token_t *token) {
  zassert_ok(wl_node_close(&node, 0));
  (void)wl_packet_tx_take(&node.port, token);
  zassert_true(wl_packet_quiescent(&node.port));
}
ZTEST(static_uart_irq, test_no_progress_no_poll_and_notify_is_not_hint) {
  setup_interval(5);
  wl_packet_token_t token = pending_tx(10);
  zassert_equal(adapter.drain_due, 15);
  for (unsigned i = 0; i < 8; ++i) {
    wl_zephyr_uart_irq_notify(&adapter);
    zassert_ok(wl_zephyr_uart_irq_service(&adapter, 10));
  }
  zassert_equal(full_data.complete_queries, 1);
  /* IRQ read zero must not authorize a query. */
  full_data.read_zero = true;
  receive_byte(2);
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 10));
  zassert_equal(full_data.complete_queries, 1);
  /* Full FIFO containing old delimiters: pause/resume is not RX progress. */
  wl_span_t room;
  zassert_ok(wl_byte_fifo_reserve(&adapter.fifo, &room));
  memset(room.data, 0, room.length);
  zassert_ok(wl_byte_fifo_commit(&adapter.fifo, room.length));
  full_data.read_zero = false;
  fire();
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 10));
  zassert_equal(full_data.complete_queries, 1);
  zassert_equal(adapter.drain_due, 15);
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 15));
  zassert_equal(full_data.complete_queries, 2);
  zassert_equal(adapter.drain_due, 20);
  finish_pending(&token);
}
static unsigned tail_notifications, early_notifications;
static void observe_tail(void *unused) { (void)unused; ++tail_notifications; }
static void consume_early_fifo_wake(void *context) {
  ++early_notifications;
  wl_zephyr_uart_irq_notify(context);
  zassert_ok(wl_zephyr_uart_irq_wait(context, K_NO_WAIT));
}
ZTEST(static_uart_irq, test_callback_tail_wake_survives_early_fifo_ack) {
  setup();
  tail_notifications = early_notifications = 0;
  adapter.notify = observe_tail;
  adapter.fifo.notify = consume_early_fifo_wake;
  for (unsigned i = 0; i < 2; ++i) {
    receive_byte(0); /* Each callback tail must still give. */
    zassert_equal(tail_notifications, i + 1);
    zassert_equal(early_notifications, i + 1);
    zassert_ok(wl_zephyr_uart_irq_wait(&adapter, K_NO_WAIT));
    zassert_equal(wl_zephyr_uart_irq_wait(&adapter, K_NO_WAIT), WL_ERR_NO_DATA);
  }
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 0));
  zassert_equal(full_data.complete_queries, 0);
  zassert_ok(wl_node_close(&node, 0));
}
ZTEST(static_uart_irq, test_failed_rx_and_large_interval_wrap) {
  setup(); wl_packet_token_t token = pending_tx(0);
  full_data.read_error = -EIO;
  receive_byte(0);
  zassert_equal(wl_zephyr_uart_irq_service(&adapter, 0), WL_ERR_IO);
  zassert_equal(full_data.complete_queries, 1);
  finish_pending(&token);
  setup_interval(1000); token = pending_tx(UINT32_MAX - 10U);
  zassert_equal(adapter.drain_due, 989);
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 0));
  zassert_equal(full_data.complete_queries, 1);
  full_data.line_idle = 1;
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 988));
  zassert_equal(full_data.complete_queries, 1);
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 989));
  zassert_equal(full_data.complete_queries, 2);
  zassert_equal(wl_packet_tx_take(&node.port, &token), WL_PACKET_DRAINED);
  zassert_ok(wl_node_close(&node, 0));
}
static void unused_callback(void) {}
static void completed_call(wl_endpoint_t *k, const wl_endpoint_call_t *c, wl_rpc_result_t r,
                           const uint8_t *p, size_t n) {
  (void)k; (void)c; (void)r; (void)p; (void)n;
}
ZTEST(static_uart_irq, test_endpoint_queued_frame_waits_for_real_drain) {
  setup();
  uint8_t value[4] = {1, 2, 3, 4};
  uint8_t *frame;
  size_t capacity = 0U;
  zassert_ok(wl_endpoint_msg_claim(&node.core, &frame, &capacity));
  zassert_true(capacity >= WL_WIRE_NATIVE_HEADER + 4U);
  memcpy(frame + WL_WIRE_NATIVE_HEADER, value, 4U);
  zassert_ok(wl_wire_frame_seal(frame, capacity, WL_WIRE_MSG, 22U, 4U));
  zassert_ok(wl_endpoint_msg_commit(&node.core, WL_WIRE_NATIVE_HEADER + 4U));
  zassert_ok(wl_endpoint_step(&node.core, 0, 16));
  full_data.line_idle = 0; fire();
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 0));
  zassert_ok(wl_endpoint_step(&node.core, 0, 16)); /* Consumes CONSUMED, not DRAINED. */
  uint8_t slot;
  uint8_t *payload;
  zassert_ok(wl_endpoint_call_claim(&node.core, &slot, &payload, &capacity));
  zassert_true(capacity >= 4U);
  memcpy(payload, value, 4U);
  wl_call_handle_t h;
  zassert_ok(wl_endpoint_call_commit(&node.core, slot, 23U, 24U, 4U, 0U, 2000U, 1U,
    completed_call, unused_callback, NULL, &h));
  zassert_ok(wl_endpoint_step(&node.core, 0, 16));
  zassert_equal(node.port.tx_sequence, 1);
  full_data.line_idle = 1; /* No new RX: both policies must retain fallback. */
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 0));
  zassert_equal(node.port.tx_sequence, 1); /* Port is active until core takes DRAINED. */
  zassert_ok(wl_endpoint_step(&node.core, 0, 16));
  zassert_equal(node.port.tx_sequence, 1);
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 1));
  zassert_ok(wl_endpoint_step(&node.core, 1, 16));
  zassert_equal(node.port.tx_sequence, 2);
  fire(); zassert_ok(wl_zephyr_uart_irq_service(&adapter, 1));
  zassert_ok(wl_endpoint_step(&node.core, 1, 16));
  zassert_ok(wl_node_close(&node, 1));
  zassert_true(wl_endpoint_quiescent(&node.core));
  zassert_true(wl_packet_quiescent(&node.port));
}
ZTEST_SUITE(static_uart_irq, NULL, NULL, NULL, NULL, NULL);

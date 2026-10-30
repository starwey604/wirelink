/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/ztest.h>
#include "wirelink/frame.h"
#include "wirelink/kernel_wire.h"
#include "wirelink/zephyr/kernel_uart_irq.h"

struct fake_uart {
  uart_irq_callback_user_data_t callback;
  void *user_data;
  uint8_t rx[128], tx[128];
  size_t rx_length, rx_offset, tx_length;
  bool rx_enabled, tx_enabled, pending, read_zero, fill_zero;
  int line_idle, error;
  unsigned reads, fills;
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

static wl_kernel_uart_irq_t adapter;
static wl_packet_port_t port;
static wl_packet_stream_t stream;
static wl_byte_fifo_t fifo;
static uint8_t rx[2][96], tx[96], bytes[16];
static uint64_t instance;
static void setup(void) {
  memset(&full_data, 0, sizeof(full_data));
  full_data.line_idle = 1;
  zassert_ok(wl_byte_fifo_init(&fifo, (wl_span_t){bytes, sizeof(bytes)}, wl_kernel_uart_irq_notify,
                               &adapter));
  wl_packet_config_t pc = {.instance = ++instance,
                           .rx_storage = &rx[0][0],
                           .rx_unit_size = 96,
                           .rx_storage_size = sizeof(rx),
                           .submit = wl_packet_stream_cobs_crc16_submit,
                           .notify = wl_packet_stream_notify,
                           .user = &stream};
  zassert_ok(wl_packet_init(&port, &pc));
  wl_packet_stream_config_t sc = {.port = &port,
                                  .tx_storage = {tx, sizeof(tx)},
                                  .maximum_frame_size = 64,
                                  .physical_submit = wl_kernel_uart_irq_submit,
                                  .notify = wl_kernel_uart_irq_notify,
                                  .user = &adapter};
  zassert_ok(wl_packet_stream_cobs_crc16_init(&stream, &sc));
  wl_kernel_uart_irq_config_t config = {
      .uart = DEVICE_GET(fake_uart_full), .port = &port, .fifo = &fifo, .stream = &stream};
  zassert_ok(wl_kernel_uart_irq_init(&adapter, &config));
  zassert_ok(wl_kernel_uart_irq_start(&adapter));
}
static void fire(void) {
  full_data.pending = true;
  full_data.callback(DEVICE_GET(fake_uart_full), full_data.user_data);
}
ZTEST(static_uart_irq, test_staged_rx_physical_tx_and_old_callback) {
  setup();
  uint8_t packet[41];
  memset(packet + 4, 55, 37);
  zassert_ok(wl_kernel_native_none_seal(packet, sizeof(packet), 22, 37));
  wl_packet_token_t token;
  zassert_ok(wl_packet_tx_submit(&port, packet, sizeof(packet), &token));
  full_data.line_idle = 0;
  fire();
  zassert_ok(wl_kernel_uart_irq_service(&adapter, 0));
  zassert_equal(wl_packet_tx_take(&port, &token), WL_PACKET_CONSUMED);
  zassert_true(wl_packet_tx_active(&port));
  zassert_false(full_data.tx_enabled);
  full_data.line_idle = 1;
  zassert_ok(wl_kernel_uart_irq_service(&adapter, 1));
  zassert_equal(wl_packet_tx_take(&port, &token), WL_PACKET_DRAINED);
  memcpy(full_data.rx, full_data.tx, full_data.tx_length);
  full_data.rx_length = full_data.tx_length;
  fire();
  zassert_false(wl_packet_rx_ready(&port)); // IRQ staged bytes, no framing/parser.
  for (unsigned i = 0; i < 20 && !wl_packet_rx_ready(&port); ++i) {
    zassert_ok(wl_kernel_uart_irq_service(&adapter, i + 2));
    fire();
  }
  zassert_ok(wl_kernel_uart_irq_service(&adapter, 24));
  wl_packet_borrow_t borrow;
  zassert_ok(wl_packet_rx_borrow(&port, &borrow));
  zassert_equal(borrow.length, sizeof(packet));
  zassert_mem_equal(borrow.data, packet, sizeof(packet));
  zassert_ok(wl_packet_rx_release(&port, &borrow));
  uart_irq_callback_user_data_t old = full_data.callback;
  void *old_user = full_data.user_data;
  wl_kernel_uart_irq_stop(&adapter);
  zassert_true(wl_packet_quiescent(&port));
  setup();
  old(DEVICE_GET(fake_uart_full), old_user);
  zassert_false(wl_byte_fifo_readable(&fifo));
  wl_kernel_uart_irq_stop(&adapter);
  zassert_true(wl_packet_quiescent(&port));
}
ZTEST_SUITE(static_uart_irq, NULL, NULL, NULL, NULL, NULL);

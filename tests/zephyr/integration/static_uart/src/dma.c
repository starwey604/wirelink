/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "wirelink/frame.h"
#include "wirelink/zephyr/kernel_uart_dma.h"

#define TEST_MAX_PAYLOAD 64U
#define TEST_STORAGE_SIZE 128U

struct fake_uart_data {
  uart_callback_t callback;
  void *callback_data;
  uint8_t *rx_buf;
  size_t rx_len;
  uint8_t *rx_next_buf;
  size_t rx_next_len;
  const uint8_t *tx_buf;
  size_t tx_len;
  int next_tx_result;
  bool rx_enabled;
  bool tx_active;
  bool tx_idle;
  size_t callback_sets;
  size_t rx_enables;
  size_t rx_buf_responses;
  size_t rx_disabled_events;
  size_t rx_disables;
  size_t tx_calls;
  size_t tx_aborts;
};

static struct fake_uart_data fake_data;
K_SEM_DEFINE(callback_detached, 0, 1);
K_SEM_DEFINE(callback_entered, 0, 1);
K_SEM_DEFINE(callback_continue, 0, 1);
K_SEM_DEFINE(quiesce_done, 0, 1);
K_THREAD_STACK_DEFINE(callback_stack, 2048);
K_THREAD_STACK_DEFINE(quiesce_stack, 2048);
static struct device_state fake_state = {
    .init_res = 0,
    .initialized = 1,
};

static int fake_callback_set(const struct device *dev, uart_callback_t callback, void *user_data) {
  struct fake_uart_data *data = dev->data;

  data->callback = callback;
  data->callback_data = user_data;
  data->callback_sets++;
  if (callback == NULL)
    k_sem_give(&callback_detached);
  return 0;
}

static int fake_tx(const struct device *dev, const uint8_t *buf, size_t len, int32_t timeout) {
  struct fake_uart_data *data = dev->data;
  int ret = data->next_tx_result;

  ARG_UNUSED(timeout);
  data->next_tx_result = 0;
  data->tx_calls++;
  if (ret != 0) {
    return ret;
  }
  if (data->tx_active) {
    return -EBUSY;
  }
  data->tx_buf = buf;
  data->tx_len = len;
  data->tx_active = true;
  data->tx_idle = false;
  return 0;
}

static int fake_irq_tx_complete(const struct device *dev) {
  struct fake_uart_data *data = dev->data;

  return data->tx_idle ? 1 : 0;
}

static int fake_tx_abort(const struct device *dev) {
  struct fake_uart_data *data = dev->data;

  data->tx_aborts++;
  return data->tx_active ? 0 : -EFAULT;
}

static int fake_rx_enable(const struct device *dev, uint8_t *buf, size_t len, int32_t timeout) {
  struct fake_uart_data *data = dev->data;
  struct uart_event event = {.type = UART_RX_BUF_REQUEST};

  ARG_UNUSED(timeout);
  if (data->rx_enabled) {
    return -EBUSY;
  }
  data->rx_buf = buf;
  data->rx_len = len;
  data->rx_enabled = true;
  data->rx_enables++;
  if (data->callback != NULL) {
    data->callback(dev, &event, data->callback_data);
  }
  return 0;
}

static int fake_rx_buf_rsp(const struct device *dev, uint8_t *buf, size_t len) {
  struct fake_uart_data *data = dev->data;

  if (!data->rx_enabled || data->rx_next_buf != NULL) {
    return -EBUSY;
  }
  data->rx_next_buf = buf;
  data->rx_next_len = len;
  data->rx_buf_responses++;
  return 0;
}

static int fake_rx_disable(const struct device *dev) {
  struct fake_uart_data *data = dev->data;

  data->rx_disables++;
  return data->rx_enabled ? 0 : -EFAULT;
}

static DEVICE_API(uart, fake_api) = {
    .callback_set = fake_callback_set,
    .tx = fake_tx,
    .tx_abort = fake_tx_abort,
    .irq_tx_complete = fake_irq_tx_complete,
    .rx_enable = fake_rx_enable,
    .rx_buf_rsp = fake_rx_buf_rsp,
    .rx_disable = fake_rx_disable,
};

static struct device fake_uart = {
    .api = &fake_api,
    .data = &fake_data,
    .state = &fake_state,
};

static wl_kernel_uart_dma_t adapter;
static wl_packet_port_t port;
static wl_packet_stream_t stream;
static wl_byte_fifo_t fifo;
static uint8_t rx[2][96], tx[96], bytes[128], dma[2][128];
static void emit(struct uart_event *event) {
  fake_data.callback(&fake_uart, event, fake_data.callback_data);
}
static uint64_t instance = 1000U;
static void setup(void) {
  memset(&fake_data, 0, sizeof(fake_data));
  zassert_ok(wl_byte_fifo_init(&fifo, (wl_span_t){bytes, sizeof(bytes)}, wl_kernel_uart_dma_notify,
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
                                  .physical_submit = wl_kernel_uart_dma_submit,
                                  .notify = wl_kernel_uart_dma_notify,
                                  .user = &adapter};
  zassert_ok(wl_packet_stream_cobs_crc16_init(&stream, &sc));
  wl_kernel_uart_dma_config_t config = {.uart = &fake_uart,
                                        .port = &port,
                                        .fifo = &fifo,
                                        .stream = &stream,
                                        .rx_buffers = {{dma[0], 128}, {dma[1], 128}},
                                        .timeout_us = 100,
                                        .tx_timeout_us = SYS_FOREVER_US,
                                        .wait_for_tx_idle = true};
  zassert_ok(wl_kernel_uart_dma_init(&adapter, &config));
  zassert_ok(wl_kernel_uart_dma_start(&adapter));
}
ZTEST(static_uart_dma, test_callbacks_stage_and_stop_requires_real_returns) {
  setup();
  uint8_t packet[12] = {0xa4, 0, 0, 22, 1, 2, 3, 4, 5, 6, 7, 8};
  wl_packet_token_t token;
  zassert_ok(wl_packet_tx_submit(&port, packet, sizeof(packet), &token));
  size_t length = fake_data.tx_len;
  memcpy(fake_data.rx_buf, fake_data.tx_buf, length);
  struct uart_event ready = {.type = UART_RX_RDY,
                             .data.rx = {.buf = fake_data.rx_buf, .offset = 0, .len = length}};
  emit(&ready);
  zassert_false(wl_packet_rx_ready(&port));
  zassert_ok(wl_kernel_uart_dma_service(&adapter, 0));
  wl_packet_borrow_t borrow;
  zassert_ok(wl_packet_rx_borrow(&port, &borrow));
  zassert_equal(borrow.length, sizeof(packet));
  zassert_mem_equal(borrow.data, packet, sizeof(packet));
  zassert_ok(wl_packet_rx_release(&port, &borrow));
  struct uart_event done = {.type = UART_TX_DONE,
                            .data.tx = {.buf = fake_data.tx_buf, .len = fake_data.tx_len}};
  fake_data.tx_active = false;
  emit(&done);
  zassert_ok(wl_kernel_uart_dma_service(&adapter, 0));
  zassert_equal(wl_packet_tx_take(&port, &token), WL_PACKET_CONSUMED);
  zassert_true(wl_packet_tx_active(&port));
  fake_data.tx_idle = true;
  zassert_ok(wl_kernel_uart_dma_service(&adapter, 1));
  zassert_equal(wl_packet_tx_take(&port, &token), WL_PACKET_DRAINED);
  zassert_ok(wl_packet_tx_submit(&port, packet, sizeof(packet), &token));
  zassert_ok(wl_kernel_uart_dma_stop(&adapter));
  zassert_equal(wl_kernel_uart_dma_service(&adapter, 2), WL_ERR_WOULD_BLOCK);
  // RX disable acknowledgement alone does not return the two DMA buffers.
  struct uart_event disabled = {.type = UART_RX_DISABLED};
  fake_data.rx_enabled = false;
  emit(&disabled);
  zassert_equal(wl_kernel_uart_dma_service(&adapter, 2), WL_ERR_WOULD_BLOCK);
  for (unsigned i = 0; i < 2; ++i) {
    struct uart_event released = {.type = UART_RX_BUF_RELEASED, .data.rx_buf = {.buf = dma[i]}};
    emit(&released);
  }
  zassert_equal(wl_kernel_uart_dma_service(&adapter, 2), WL_ERR_WOULD_BLOCK);
  struct uart_event aborted = {.type = UART_TX_ABORTED,
                               .data.tx = {.buf = fake_data.tx_buf, .len = 0}};
  fake_data.tx_active = false;
  emit(&aborted);
  zassert_ok(wl_kernel_uart_dma_service(&adapter, 2));
  zassert_true(wl_packet_tx_take(&port, &token) & WL_PACKET_TX_ERROR);
  uart_callback_t old = fake_data.callback;
  void *old_user = fake_data.callback_data;
  wl_kernel_uart_dma_quiesce(&adapter);
  zassert_true(wl_packet_quiescent(&port));
  old(&fake_uart, &ready, old_user);
  zassert_false(wl_byte_fifo_readable(&fifo));
}
static struct k_thread callback_thread, quiesce_thread;
static void suspend_callback(void *unused) {
  (void)unused;
  if (k_current_get() != &callback_thread)
    return;
  k_sem_give(&callback_entered);
  (void)k_sem_take(&callback_continue, K_FOREVER);
}
static void finish_callback(void *a, void *b, void *c) {
  (void)a;
  (void)b;
  (void)c;
  struct uart_event disabled = {.type = UART_RX_DISABLED};
  fake_data.rx_enabled = false;
  emit(&disabled);
}
static void close_adapter(void *a, void *b, void *c) {
  (void)a;
  (void)b;
  (void)c;
  wl_kernel_uart_dma_quiesce(&adapter);
  k_sem_give(&quiesce_done);
}
ZTEST(static_uart_dma, test_quiesce_joins_entered_callback_with_full_fifo) {
  setup();
  memset(dma[0], 77, sizeof(dma[0]));
  struct uart_event ready = {.type = UART_RX_RDY,
                             .data.rx = {.buf = dma[0], .offset = 0, .len = sizeof(dma[0])}};
  emit(&ready);
  zassert_false(wl_byte_fifo_has_space(&fifo));
  zassert_ok(wl_kernel_uart_dma_stop(&adapter));
  for (unsigned i = 0; i < 2; ++i) {
    struct uart_event released = {.type = UART_RX_BUF_RELEASED, .data.rx_buf = {.buf = dma[i]}};
    emit(&released);
  }
  k_sem_reset(&callback_detached);
  k_sem_reset(&callback_entered);
  k_sem_reset(&callback_continue);
  k_sem_reset(&quiesce_done);
  adapter.config.notify = suspend_callback;
  k_thread_create(&callback_thread, callback_stack, K_THREAD_STACK_SIZEOF(callback_stack),
                  finish_callback, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
  zassert_ok(k_sem_take(&callback_entered, K_SECONDS(1)));
  k_thread_create(&quiesce_thread, quiesce_stack, K_THREAD_STACK_SIZEOF(quiesce_stack),
                  close_adapter, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
  int detached = k_sem_take(&callback_detached, K_SECONDS(1));
  int early = k_sem_take(&quiesce_done, K_NO_WAIT);
  k_sem_give(&callback_continue);
  zassert_ok(detached);
  zassert_not_equal(early, 0);
  zassert_ok(k_sem_take(&quiesce_done, K_SECONDS(1)));
  zassert_ok(k_thread_join(&callback_thread, K_SECONDS(1)));
  zassert_ok(k_thread_join(&quiesce_thread, K_SECONDS(1)));
  zassert_true(adapter.detached);
  zassert_is_null(fake_data.callback);
  zassert_true(wl_packet_quiescent(&port));
  zassert_true(wl_byte_fifo_readable(&fifo)); // Unpublished bytes never block shutdown.
}
ZTEST_SUITE(static_uart_dma, NULL, NULL, NULL, NULL, NULL);

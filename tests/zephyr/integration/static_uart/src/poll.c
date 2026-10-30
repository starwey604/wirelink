/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/zephyr/kernel_uart_poll.h"
#include <zephyr/drivers/uart.h>
#include <zephyr/ztest.h>
#include <string.h>
static wl_kernel_uart_poll_t adapter;
static wl_packet_port_t port;
static wl_packet_stream_t stream;
static wl_byte_fifo_t fifo;
static uint8_t rx[2][96], wire[96], staging[16], input[128], output[128];
static size_t input_length, input_offset, output_length;
static struct k_spinlock lock;
static atomic_t idle;
K_THREAD_STACK_DEFINE(stack, 2048);
static int initialize(const struct device *d) {
  (void)d;
  return 0;
}
static int read_byte(const struct device *d, unsigned char *out) {
  (void)d;
  k_spinlock_key_t key = k_spin_lock(&lock);
  int result = input_offset < input_length ? 0 : -1;
  if (!result)
    *out = input[input_offset++];
  k_spin_unlock(&lock, key);
  return result;
}
static void write_byte(const struct device *d, unsigned char byte) {
  (void)d;
  zassert_true(output_length < sizeof(output));
  output[output_length++] = byte;
}
static int line_idle(const struct device *d) {
  (void)d;
  return atomic_get(&idle);
}
static DEVICE_API(uart, poll_api) = {.poll_in = read_byte, .poll_out = write_byte};
DEVICE_DEFINE(packet_poll_uart, "packet_poll_uart", initialize, NULL, NULL, NULL, POST_KERNEL,
              CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &poll_api);
ZTEST(static_uart_poll, test_worker_stages_and_stop_waits_for_physical_idle) {
  zassert_ok(wl_byte_fifo_init(&fifo, (wl_span_t){staging, sizeof(staging)},
                               wl_kernel_uart_poll_notify, &adapter));
  wl_packet_config_t pc = {.instance = 3001,
                           .rx_storage = &rx[0][0],
                           .rx_unit_size = 96,
                           .rx_storage_size = sizeof(rx),
                           .submit = wl_packet_stream_cobs_crc16_submit,
                           .notify = wl_packet_stream_notify,
                           .user = &stream};
  zassert_ok(wl_packet_init(&port, &pc));
  wl_packet_stream_config_t sc = {.port = &port,
                                  .tx_storage = {wire, sizeof(wire)},
                                  .maximum_frame_size = 64,
                                  .physical_submit = wl_kernel_uart_poll_submit,
                                  .notify = wl_kernel_uart_poll_notify,
                                  .user = &adapter};
  zassert_ok(wl_packet_stream_cobs_crc16_init(&stream, &sc));
  wl_kernel_uart_poll_config_t config = {.uart = DEVICE_GET(packet_poll_uart),
                                         .port = &port,
                                         .fifo = &fifo,
                                         .stream = &stream,
                                         .stack = stack,
                                         .stack_size = K_THREAD_STACK_SIZEOF(stack),
                                         .priority = -1,
                                         .tx_idle = line_idle};
  zassert_ok(wl_kernel_uart_poll_init(&adapter, &config));
  zassert_ok(wl_kernel_uart_poll_start(&adapter));
  uint8_t packet[41] = {0xa4, 0, 0, 22};
  memset(packet + 4, 77, 37);
  wl_packet_token_t token;
  zassert_ok(wl_packet_tx_submit(&port, packet, sizeof(packet), &token));
  zassert_equal(wl_packet_tx_take(&port, &token), WL_PACKET_CONSUMED);
  zassert_true(wl_packet_tx_active(&port));
  k_spinlock_key_t key = k_spin_lock(&lock);
  memcpy(input, output, output_length);
  input_length = output_length;
  k_spin_unlock(&lock, key);
  k_sleep(K_TICKS(2));
  zassert_false(wl_packet_rx_ready(&port));
  for (unsigned i = 0; i < 100 && !wl_packet_rx_ready(&port); ++i) {
    zassert_ok(wl_kernel_uart_poll_service(&adapter, k_uptime_get_32()));
    k_sleep(K_TICKS(1));
  }
  wl_packet_borrow_t borrow;
  zassert_ok(wl_packet_rx_borrow(&port, &borrow));
  zassert_equal(borrow.length, sizeof(packet));
  zassert_mem_equal(borrow.data, packet, sizeof(packet));
  zassert_ok(wl_packet_rx_release(&port, &borrow));
  zassert_equal(wl_kernel_uart_poll_stop(&adapter), WL_ERR_WOULD_BLOCK);
  zassert_false(adapter.worker_created);
  zassert_false(wl_packet_quiescent(&port));
  atomic_set(&idle, 1);
  zassert_ok(wl_kernel_uart_poll_stop(&adapter));
  zassert_equal(wl_packet_tx_take(&port, &token), WL_PACKET_DRAINED);
  zassert_true(wl_packet_quiescent(&port));
  zassert_equal(wl_kernel_uart_poll_wait(&adapter, K_NO_WAIT), WL_ERR_CANCELLED);
}
ZTEST_SUITE(static_uart_poll, NULL, NULL, NULL, NULL, NULL);

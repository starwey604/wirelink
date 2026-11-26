/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/zephyr/uart_poll.h"
#include <zephyr/drivers/uart.h>
#include <zephyr/ztest.h>
#include "fixture.h"
#include "wirelink/zephyr/node.h"
#include <string.h>
static wl_zephyr_uart_poll_t adapter;
static wl_node_t node;
static wl_packet_stream_t stream;
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
  wl_zephyr_uart_poll_config_t config = {.uart = DEVICE_GET(packet_poll_uart),
                                         .fifo_storage = {staging, sizeof(staging)},
                                         .stack = stack,
                                         .stack_size = K_THREAD_STACK_SIZEOF(stack),
                                         .priority = -1,
                                         .tx_idle = line_idle};
  zassert_ok(bind_uart(&node, &stream, 3001, (wl_span_t){rx[0], sizeof(rx)},
      (wl_span_t){wire, sizeof(wire)}, wl_zephyr_uart_poll_transport(&adapter, &config)));
  uint8_t packet[41] = {0x57, 0, 22, 0};
  memset(packet + 4, 77, 37);
  wl_packet_token_t token;
  zassert_ok(wl_packet_tx_submit(&node.port, packet, sizeof(packet), &token));
  zassert_equal(wl_packet_tx_take(&node.port, &token), WL_PACKET_CONSUMED);
  zassert_true(wl_packet_tx_active(&node.port));
  k_spinlock_key_t key = k_spin_lock(&lock);
  memcpy(input, output, output_length);
  input_length = output_length;
  k_spin_unlock(&lock, key);
  k_sleep(K_TICKS(2));
  zassert_false(wl_packet_rx_ready(&node.port));
  for (unsigned i = 0; i < 100 && !wl_packet_rx_ready(&node.port); ++i) {
    zassert_ok(wl_zephyr_uart_poll_service(&adapter, k_uptime_get_32()));
    k_sleep(K_TICKS(1));
  }
  wl_packet_borrow_t borrow;
  zassert_ok(wl_packet_rx_borrow(&node.port, &borrow));
  zassert_equal(borrow.length, sizeof(packet));
  zassert_mem_equal(borrow.data, packet, sizeof(packet));
  zassert_ok(wl_packet_rx_release(&node.port, &borrow));
  zassert_equal(wl_node_close(&node, k_uptime_get_32()), WL_ERR_WOULD_BLOCK);
  zassert_false(adapter.worker_created);
  zassert_false(wl_packet_quiescent(&node.port));
  zassert_equal(wl_zephyr_node_close(&node, 0), WL_ERR_TIMEOUT);
  atomic_set(&idle, 1);
  zassert_equal(wl_node_close(&node, k_uptime_get_32()), WL_ERR_BUSY);
  zassert_equal(wl_packet_tx_take(&node.port, &token), WL_PACKET_DRAINED);
  zassert_ok(wl_zephyr_node_close(&node, 0));
  zassert_true(wl_packet_quiescent(&node.port));
  zassert_equal(wl_zephyr_uart_poll_wait(&adapter, K_NO_WAIT), WL_ERR_CANCELLED);
}
ZTEST_SUITE(static_uart_poll, NULL, NULL, NULL, NULL, NULL);

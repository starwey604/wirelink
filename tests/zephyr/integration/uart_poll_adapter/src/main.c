/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/ztest.h>
#include "wirelink/wirelink.h"
#include "wirelink/frame.h"
#include "wirelink/zephyr/uart_poll.h"
#include "wirelink/wait.h"

static wl_zephyr_uart_poll_t adapter;
static wl_endpoint_t endpoint;
K_THREAD_STACK_DEFINE(rx_stack, 2048);
static uint8_t tx_payload[1100], tx_unit[1200], control[64], fallback[1200], fifo[2048];
static uint8_t input[8192], output[8192];
static struct k_spinlock uart_lock;
static size_t input_size, input_offset, output_size, stop_after;
static atomic_t reads, notifications, hardware_error;
static bool fail_read, slow_tx;
static int fake_init(const struct device *dev) { (void)dev; return 0; }
static int poll_in(const struct device *dev, unsigned char *byte) {
  (void)dev;
  atomic_inc(&reads);
  k_spinlock_key_t key = k_spin_lock(&uart_lock);
  int result = fail_read ? -EIO : input_offset < input_size ? 0 : -1;
  if (result == 0) *byte = input[input_offset++];
  k_spin_unlock(&uart_lock, key);
  return result;
}
static void poll_out(const struct device *dev, unsigned char byte) {
  (void)dev;
  if (slow_tx) k_busy_wait(10);
  output[output_size++] = byte;
  if (stop_after != 0 && output_size == stop_after) wl_zephyr_uart_poll_request_stop(&adapter);
}
static int err_check(const struct device *dev) { (void)dev; return atomic_set(&hardware_error, 0); }
static DEVICE_API(uart, api) = {.poll_in = poll_in, .poll_out = poll_out, .err_check = err_check};
DEVICE_DEFINE(fake_poll_uart, "fake_poll_uart", fake_init, NULL, NULL, NULL,
    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);
static bool inspect_wait, notify_in_clock;
static k_timeout_t observed_timeout;
static void (*wait_entry)(void);
int __real_z_impl_k_sem_take(struct k_sem *, k_timeout_t);
int __wrap_z_impl_k_sem_take(struct k_sem *sem, k_timeout_t timeout) {
  if (sem == &adapter.wake) {
    if (wait_entry != NULL) { void (*action)(void) = wait_entry; wait_entry = NULL; action(); }
    if (inspect_wait) { observed_timeout = timeout; return -EAGAIN; }
  }
  return __real_z_impl_k_sem_take(sem, timeout);
}
static void external_notify(void) {
  const wl_waiter_t *waiter = wl_endpoint_waiter(&endpoint);
  waiter->notify(waiter->user_data);
}
static DEVICE_API(uart, no_error_api) = {.poll_in = poll_in, .poll_out = poll_out};
DEVICE_DEFINE(fake_poll_unknown, "fake_poll_unknown", fake_init, NULL, NULL, NULL,
    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &no_error_api);
static wl_time_ms_t now(void *context) {
  (void)context;
  if (notify_in_clock) { notify_in_clock = false; external_notify(); }
  return k_uptime_get_32();
}
static void notify(void *context) { (void)context; atomic_inc(&notifications); }
static wl_zephyr_uart_poll_config_t port(void) {
  return (wl_zephyr_uart_poll_config_t){.uart = DEVICE_GET(fake_poll_uart),
    .stack = rx_stack, .stack_size = K_THREAD_STACK_SIZEOF(rx_stack),
    .priority = -1, .notify = notify};
}
static void initialize_endpoint(void) {
  wl_endpoint_close(&endpoint);
  memset(&endpoint, 0, sizeof(endpoint));
  input_size = input_offset = output_size = stop_after = 0;
  fail_read = slow_tx = false;
  atomic_clear(&hardware_error); atomic_clear(&reads); atomic_clear(&notifications);
  const wl_config_t config = {.max_payload_len = 1100, .envelope = WL_ENVELOPE_COBS_STREAM,
    .integrity = WL_INTEGRITY_CRC16, .session_id = 1, .ack_timeout_ms = 10};
  const wl_storage_t storage = {.tx_payload = tx_payload, .tx_payload_size = sizeof(tx_payload),
    .tx_unit = tx_unit, .tx_unit_size = sizeof(tx_unit),
    .control_unit = control, .control_unit_size = sizeof(control),
    .rx_fallback = fallback, .rx_fallback_size = sizeof(fallback),
    .rx_fifo = fifo, .rx_fifo_size = sizeof(fifo)};
  const wl_clock_t clock = {now, NULL};
  zassert_ok(wl_endpoint_init(&endpoint, &config, &storage, &clock, NULL));
}
static void initialize(void) {
  initialize_endpoint();
  wl_zephyr_uart_poll_config_t config_port = port();
  zassert_ok(wl_zephyr_uart_poll_attach(&adapter, &endpoint, &config_port));
}
static void append(const uint8_t *bytes, size_t length) {
  k_spinlock_key_t key = k_spin_lock(&uart_lock);
  zassert_true(input_size + length <= sizeof(input));
  memcpy(input + input_size, bytes, length); input_size += length;
  k_spin_unlock(&uart_lock, key);
}
static size_t consumed(void) {
  k_spinlock_key_t key = k_spin_lock(&uart_lock);
  size_t count = input_offset;
  k_spin_unlock(&uart_lock, key);
  return count;
}
static void wait_consumed(size_t count) {
  for (unsigned i = 0; consumed() < count && i < 1000; ++i) k_sleep(K_TICKS(1));
  zassert_equal(consumed(), count);
}
static int poll_event(wl_time_ms_t time, wl_event_t *event) {
  memset(event, 0, sizeof(*event));
  int result = wl_poll(wl_endpoint_link(&endpoint), time, event);
  return result == WL_ERR_NO_DATA ? WL_OK : result;
}
static size_t frame(uint8_t *wire, size_t capacity, uint16_t id, size_t length) {
  static uint8_t payload[1024];
  memset(payload, 0xa5, length);
  wl_wire_packet_t packet = {.type = WL_PACKET_DATA, .integrity = WL_INTEGRITY_CRC16,
    .message_id = id, .payload = payload, .payload_len = length};
  size_t written;
  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_COBS_STREAM, wire, capacity, &written));
  return written;
}
ZTEST(uart_poll, test_rx_partial_long_and_corruption_recovery) {
  initialize();
  uint8_t wire[1200];
  size_t length = frame(wire, sizeof(wire), 7, 1024);
  append(wire, 3);
  zassert_ok(wl_zephyr_uart_poll_start(&adapter));
  wait_consumed(3);
  zassert_true(atomic_get(&notifications) > 0); /* Partial frame wakes owner. */
  wl_event_t event;
  zassert_ok(poll_event(0, &event));
  zassert_equal(event.type, WL_EVT_NONE);
  append(wire + 3, length - 3); wait_consumed(length);
  zassert_ok(poll_event(1, &event));
  zassert_equal(event.type, WL_EVT_UNRELIABLE_RX);
  zassert_equal(event.payload_len, 1024);
  zassert_equal(event.payload[1023], 0xa5);
  wl_event_release(wl_endpoint_link(&endpoint), &event);
  wire[8] ^= 0x40;
  append(wire, length); wait_consumed(length * 2);
  zassert_ok(poll_event(2, &event));
  zassert_equal(event.type, WL_EVT_NONE);
  wire[8] ^= 0x40;
  append(wire, length); wait_consumed(length * 3);
  zassert_ok(poll_event(3, &event));
  zassert_equal(event.type, WL_EVT_UNRELIABLE_RX);
  wl_event_release(wl_endpoint_link(&endpoint), &event);
  wl_endpoint_close(&endpoint);
}
ZTEST(uart_poll, test_idle_sleep_backpressure_and_stop_join) {
  initialize(); zassert_ok(wl_zephyr_uart_poll_start(&adapter));
  k_sleep(K_MSEC(3));
  zassert_true(atomic_get(&reads) < 100); /* Empty polling really blocks. */
  uint8_t bytes[4096], wire[64];
  size_t length = frame(wire, sizeof(wire), 9, 32), total = 0;
  while (total + length <= sizeof(bytes)) { memcpy(bytes + total, wire, length); total += length; }
  append(bytes, total);
  k_sleep(K_MSEC(30));
  size_t first = consumed();
  zassert_true(first <= sizeof(fifo));
  zassert_true(first > 0);
  k_sleep(K_MSEC(3)); zassert_equal(consumed(), first); /* No read/discard when full. */
  wl_event_t event;
  for (unsigned i = 0; i < 1000 && consumed() < total; ++i) {
    zassert_ok(poll_event(i, &event));
    if (event.type != WL_EVT_NONE) wl_event_release(wl_endpoint_link(&endpoint), &event);
    k_sleep(K_TICKS(1));
  }
  wait_consumed(total);
  wl_zephyr_uart_poll_request_stop(&adapter);
  zassert_equal(wl_zephyr_uart_poll_wait(&adapter, K_FOREVER), WL_ERR_CANCELLED);
  wl_endpoint_close(&endpoint);
  zassert_false(adapter.worker_created);
  int old_reads = atomic_get(&reads), old_notify = atomic_get(&notifications);
  k_sleep(K_MSEC(2));
  zassert_equal(atomic_get(&reads), old_reads);
  zassert_equal(atomic_get(&notifications), old_notify);
  wl_zephyr_uart_poll_stop(&adapter);
  initialize(); zassert_ok(wl_zephyr_uart_poll_start(&adapter)); wl_endpoint_close(&endpoint);
}
ZTEST(uart_poll, test_tx_sync_lifetime_long_and_partial_stop) {
  initialize(); zassert_ok(wl_zephyr_uart_poll_start(&adapter));
  uint8_t payload[1024]; memset(payload, 0x77, sizeof(payload));
  zassert_ok(wl_send_unreliable(wl_endpoint_link(&endpoint), 8, payload, sizeof(payload)));
  memset(payload, 0, sizeof(payload));
  zassert_true(output_size > 1024);
  wl_event_t event;
  zassert_ok(poll_event(1, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
  wl_zephyr_uart_poll_stats_t stats; wl_zephyr_uart_poll_get_stats(&adapter, &stats);
  zassert_equal(stats.tx_bytes, output_size); zassert_equal(stats.tx_consumed, 1);
  stop_after = output_size + 9;
  zassert_equal(wl_send_unreliable(wl_endpoint_link(&endpoint), 8, payload, sizeof(payload)), WL_ERR_IO);
  wl_zephyr_uart_poll_get_stats(&adapter, &stats);
  zassert_equal(stats.tx_bytes, output_size); zassert_equal(stats.tx_consumed, 1);
  zassert_equal(stats.errors, 1); zassert_equal(output_size, stop_after);
  wl_endpoint_close(&endpoint);
}
ZTEST(uart_poll, test_wait_latch_and_capability_rejection) {
  initialize();
  const wl_waiter_t *waiter = wl_endpoint_waiter(&endpoint);
  zassert_not_null(waiter);
  zassert_ok(wl_zephyr_uart_poll_start(&adapter));
  waiter->notify(waiter->user_data);
  zassert_ok(waiter->wait(waiter->user_data, 0));
  zassert_equal(waiter->wait(waiter->user_data, 0), WL_ERR_NO_DATA);
  zassert_equal(waiter->wait(waiter->user_data, 1), WL_ERR_NO_DATA);
  wl_zephyr_uart_poll_request_stop(&adapter);
  zassert_equal(waiter->wait(waiter->user_data, UINT32_MAX), WL_ERR_CANCELLED);
  wl_endpoint_close(&endpoint);
  initialize_endpoint();
  wl_zephyr_uart_poll_config_t config = port(); config.wait_for_tx_idle = true;
  zassert_equal(wl_zephyr_uart_poll_attach(&adapter, &endpoint, &config), WL_ERR_NOT_SUPPORTED);
  zassert_false(wl_endpoint_has_adapter(&endpoint));
  wl_endpoint_close(&endpoint);
}
ZTEST(uart_poll, test_error_recovery_and_observability) {
  initialize(); atomic_set(&hardware_error, UART_ERROR_OVERRUN);
  zassert_ok(wl_zephyr_uart_poll_start(&adapter)); k_sleep(K_MSEC(2));
  wl_zephyr_uart_poll_stats_t stats; wl_zephyr_uart_poll_get_stats(&adapter, &stats);
  zassert_true(stats.error_api_available); zassert_equal(stats.errors, 1);
  zassert_ok(wl_endpoint_step(&endpoint, 16));
  uint8_t wire[64]; size_t length = frame(wire, sizeof(wire), 7, 3);
  append(wire, length); wait_consumed(length);
  wl_event_t event; zassert_ok(poll_event(1, &event));
  zassert_equal(event.type, WL_EVT_UNRELIABLE_RX); wl_event_release(wl_endpoint_link(&endpoint), &event);
  wl_endpoint_close(&endpoint);
}
ZTEST(uart_poll, test_query_and_wait_entry_notifications_timeout_bounds) {
  initialize(); zassert_ok(wl_zephyr_uart_poll_start(&adapter));
  const wl_waiter_t *waiter = wl_endpoint_waiter(&endpoint);
  notify_in_clock = true;
  wl_readiness_t hint;
  zassert_ok(wl_endpoint_get_readiness(&endpoint, &hint));
  zassert_ok(waiter->wait(waiter->user_data, 0));
  wait_entry = external_notify;
  zassert_ok(waiter->wait(waiter->user_data, 1));
  inspect_wait = true;
  const uint32_t bounds[] = {0, 1, UINT32_MAX - 1, UINT32_MAX};
  for (unsigned i = 0; i < ARRAY_SIZE(bounds); ++i) {
    zassert_equal(waiter->wait(waiter->user_data, bounds[i]), WL_ERR_NO_DATA);
    if (bounds[i] == UINT32_MAX) zassert_true(K_TIMEOUT_EQ(observed_timeout, K_FOREVER));
    else {
      zassert_false(K_TIMEOUT_EQ(observed_timeout, K_FOREVER));
      zassert_true(observed_timeout.ticks >= 0);
    }
  }
  uint32_t remaining = wl_wait_remaining(10, UINT32_MAX - 2, 2);
  zassert_equal(remaining, 5);
  zassert_equal(waiter->wait(waiter->user_data, remaining), WL_ERR_NO_DATA);
  zassert_true(observed_timeout.ticks > 0);
  zassert_equal(wl_wait_remaining(5, UINT32_MAX - 2, 2), 0);
  inspect_wait = false;
  wl_endpoint_close(&endpoint);
}
ZTEST(uart_poll, test_poll_error_and_unknown_hardware_counters) {
  initialize(); fail_read = true;
  zassert_ok(wl_zephyr_uart_poll_start(&adapter)); k_sleep(K_MSEC(2));
  wl_zephyr_uart_poll_stats_t stats;
  wl_zephyr_uart_poll_get_stats(&adapter, &stats);
  zassert_true(stats.errors > 0);
  wl_endpoint_close(&endpoint);
  initialize_endpoint();
  wl_zephyr_uart_poll_config_t config = port();
  config.uart = DEVICE_GET(fake_poll_unknown);
  zassert_ok(wl_zephyr_uart_poll_attach(&adapter, &endpoint, &config));
  zassert_ok(wl_zephyr_uart_poll_start(&adapter)); k_sleep(K_MSEC(1));
  wl_zephyr_uart_poll_get_stats(&adapter, &stats);
  zassert_false(stats.error_api_available);
  wl_endpoint_close(&endpoint);
}
ZTEST(uart_poll, test_stop_before_start_is_latched) {
  initialize();
  wl_zephyr_uart_poll_request_stop(&adapter);
  zassert_equal(wl_zephyr_uart_poll_start(&adapter), WL_ERR_CANCELLED);
  zassert_false(adapter.worker_created);
  zassert_equal(wl_zephyr_uart_poll_wait(&adapter, K_FOREVER), WL_ERR_CANCELLED);
  wl_endpoint_close(&endpoint);
  initialize();
  zassert_ok(wl_zephyr_uart_poll_start(&adapter));
  wl_endpoint_close(&endpoint);
}
ZTEST(uart_poll, test_rx_preempts_blocking_owner_tx) {
  initialize();
  uint8_t wire[1200], payload[1024];
  size_t length = frame(wire, sizeof(wire), 7, 1024);
  append(wire, length);
  int priority = k_thread_priority_get(k_current_get());
  /* Actual hardware owner is priority 0 with a 20 ms timeslice. A 10 ms
   * synchronous TX must still admit RX batches before the owner returns. */
  k_thread_priority_set(k_current_get(), 0);
  zassert_ok(wl_zephyr_uart_poll_start(&adapter));
  size_t before = consumed();
  slow_tx = true;
  memset(payload, 0x77, sizeof(payload));
  int result = wl_send_unreliable(wl_endpoint_link(&endpoint), 8, payload, sizeof(payload));
  size_t during = consumed();
  k_thread_priority_set(k_current_get(), priority);
  zassert_ok(result);
  zassert_true(during > before);
  wl_endpoint_close(&endpoint);
}
ZTEST_SUITE(uart_poll, NULL, NULL, NULL, NULL, NULL);

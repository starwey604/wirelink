/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/ztest.h>
#include "wirelink/frame.h"
#include "wirelink/wirelink.h"
#include "wirelink/zephyr/uart_irq.h"
#include "owner_wait.h"

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

static int fake_init(const struct device *device) { (void)device; return 0; }
static void fake_callback_set(const struct device *device,
                              uart_irq_callback_user_data_t callback, void *user_data) {
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
  if (data->on_complete != NULL) data->on_complete();
  return data->line_idle;
}
static int fake_error(const struct device *device) {
  struct fake_uart *data = device->data;
  int error = data->error; data->error = 0; return error;
}
static void fake_update(const struct device *device) { (void)device; }
static int fake_pending(const struct device *device) {
  struct fake_uart *data = device->data;
  bool pending = data->pending;
  data->pending = false;
  return pending || (data->level_pending &&
      (fake_rx_ready(device) || fake_tx_ready(device)));
}
static int fake_read(const struct device *device, uint8_t *out, int capacity) {
  struct fake_uart *data = device->data;
  ++data->reads;
  if (data->on_read != NULL) data->on_read();
  if (data->read_error) return data->read_error;
  if (data->read_zero) return 0;
  size_t size = MIN((size_t)capacity, data->rx_length - data->rx_offset);
  if (data->read_chunk != 0U) size = MIN(size, data->read_chunk);
  memcpy(out, data->rx + data->rx_offset, size);
  data->rx_offset += size;
  return (int)size;
}
static int fake_fill(const struct device *device, const uint8_t *input, int length) {
  struct fake_uart *data = device->data;
  ++data->fills;
  if (data->fill_error) return data->fill_error;
  if (data->fill_zero) return 0;
  if (data->fill_chunk != 0U) length = MIN((size_t)length, data->fill_chunk);
  zassert_true(data->tx_length + (size_t)length <= sizeof(data->tx));
  memcpy(data->tx + data->tx_length, input, (size_t)length);
  data->tx_length += (size_t)length;
  return length;
}
#define COMMON_API \
  .irq_callback_set = fake_callback_set, .irq_rx_enable = fake_rx_enable, \
  .irq_rx_disable = fake_rx_disable, .irq_tx_enable = fake_tx_enable, \
  .irq_tx_disable = fake_tx_disable, .irq_rx_ready = fake_rx_ready, \
  .irq_tx_ready = fake_tx_ready, .irq_update = fake_update, .irq_is_pending = fake_pending

static DEVICE_API(uart, full_api) = {
  COMMON_API, .fifo_read = fake_read, .fifo_fill = fake_fill, .irq_tx_complete = fake_complete, .err_check = fake_error,
};
static DEVICE_API(uart, cdc_api) = {
  COMMON_API, .fifo_read = fake_read, .fifo_fill = fake_fill,
};
static DEVICE_API(uart, missing_api) = { COMMON_API, .irq_tx_complete = fake_complete };
static DEVICE_API(uart, no_callback_api) = { .irq_update = fake_update };
DEVICE_DEFINE(fake_uart_full, "fake_uart_full", fake_init, NULL, &full_data, NULL,
    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &full_api);
DEVICE_DEFINE(fake_uart_cdc, "fake_uart_cdc", fake_init, NULL, &cdc_data, NULL,
    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &cdc_api);
DEVICE_DEFINE(fake_uart_missing, "fake_uart_missing", fake_init, NULL, &missing_data, NULL,
    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &missing_api);
DEVICE_DEFINE(fake_uart_no_callback, "fake_uart_no_callback", fake_init, NULL,
    &no_callback_data, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &no_callback_api);

static wl_ctx_t link;
static wl_zephyr_uart_irq_t adapter;
static uint8_t tx_payload[16], tx_unit[64], control[64], fallback[64], fifo[64];
static bool inspect_timeout;
static unsigned inspected_waits;
static k_timeout_t inspected_timeout;
static void (*on_wait_entry)(void);
int __real_z_impl_k_sem_take(struct k_sem *, k_timeout_t);
int __wrap_z_impl_k_sem_take(struct k_sem *sem, k_timeout_t timeout) {
  if (sem == &adapter.wake && on_wait_entry != NULL) {
    void (*action)(void) = on_wait_entry;
    on_wait_entry = NULL;
    action();
  }
  if (inspect_timeout && sem == &adapter.wake) {
    inspected_timeout = timeout;
    ++inspected_waits;
    return -EAGAIN;
  }
  return __real_z_impl_k_sem_take(sem, timeout);
}
static void initialize_link(void) {
  wl_config_t config = {.envelope = WL_ENVELOPE_COBS_STREAM, .integrity = WL_INTEGRITY_CRC16,
    .max_payload_len = sizeof(tx_payload), .session_id = 0x1082679ab344bcadULL};
  wl_storage_t storage = {.tx_payload = tx_payload, .tx_payload_size = sizeof(tx_payload),
    .tx_unit = tx_unit, .tx_unit_size = sizeof(tx_unit), .control_unit = control,
    .control_unit_size = sizeof(control), .rx_fallback = fallback,
    .rx_fallback_size = sizeof(fallback), .rx_fifo = fifo, .rx_fifo_size = sizeof(fifo)};
  memset(&link, 0, sizeof(link));
  zassert_ok(wl_init(&link, &config, &storage));
}
static void initialize_adapter(const struct device *device) {
  wl_zephyr_uart_irq_stop(&adapter);
  initialize_link();
  memset(device->data, 0, sizeof(struct fake_uart));
  ((struct fake_uart *)device->data)->line_idle = 1;
  wl_zephyr_uart_irq_config_t config = {.uart = device, .link = &link};
  zassert_ok(wl_zephyr_uart_irq_init(&adapter, &config));
  zassert_ok(wl_zephyr_uart_irq_start(&adapter));
}
static void fire(const struct device *device) {
  struct fake_uart *data = device->data;
  zassert_not_null(data->callback);
  data->pending = true;
  data->callback(device, data->user_data);
}

ZTEST(wirelink_uart_irq_adapter, test_receive_and_release) {
  const struct device *device = DEVICE_GET(fake_uart_full);
  initialize_adapter(device);
  const uint8_t payload[] = {0, 7, 19};
  wl_wire_packet_t packet = {.type = WL_PACKET_DATA, .integrity = WL_INTEGRITY_CRC16,
    .message_id = 11, .payload = payload, .payload_len = sizeof(payload)};
  zassert_ok(wl_frame_encode(&packet, WL_ENVELOPE_COBS_STREAM, full_data.rx,
      sizeof(full_data.rx), &full_data.rx_length));
  fire(device);
  wl_event_t event;
  zassert_ok(wl_poll(&link, 1, &event));
  zassert_equal(event.type, WL_EVT_UNRELIABLE_RX);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  wl_event_release(&link, &event);
  wl_zephyr_uart_irq_stats_t stats;
  wl_zephyr_uart_irq_get_stats(&adapter, &stats);
  zassert_equal(stats.rx_bytes, full_data.rx_length);
  zassert_equal(stats.rx_irqs, 1);
  zassert_equal(stats.errors, 0);
}

ZTEST(wirelink_uart_irq_adapter, test_backpressure_pause_and_resume) {
  const struct device *device = DEVICE_GET(fake_uart_full);
  initialize_adapter(device);
  memset(full_data.rx, 0x91, sizeof(full_data.rx));
  full_data.rx_length = sizeof(full_data.rx);
  fire(device);
  fire(device);
  wl_zephyr_uart_irq_stats_t stats;
  wl_zephyr_uart_irq_get_stats(&adapter, &stats);
  zassert_true(stats.rx_paused);
  zassert_false(full_data.rx_enabled);
  zassert_ok(wl_feed_recover_reset(&link));
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 1U));
  zassert_true(full_data.rx_enabled);
  wl_zephyr_uart_irq_get_stats(&adapter, &stats);
  zassert_false(stats.rx_paused);
  zassert_equal(stats.rx_pauses, 1);
}

static void assert_transmit(const struct device *device) {
  initialize_adapter(device);
  const uint8_t payload[] = {1, 0, 11};
  zassert_ok(wl_send_unreliable(&link, 7, payload, sizeof(payload)));
  fire(device);
  struct fake_uart *data = device->data;
  zassert_false(data->tx_enabled);
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 1U));
  wl_event_t event;
  zassert_ok(wl_poll(&link, 1, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
  wl_zephyr_uart_irq_stats_t stats;
  wl_zephyr_uart_irq_get_stats(&adapter, &stats);
  zassert_equal(stats.tx_bytes, data->tx_length);
  zassert_equal(stats.tx_consumed, 1);
  zassert_equal(stats.errors, 0);
  zassert_false(stats.tx_active);
}
ZTEST(wirelink_uart_irq_adapter, test_physical_completion) {
  assert_transmit(DEVICE_GET(fake_uart_full));
}
ZTEST(wirelink_uart_irq_adapter, test_cdc_missing_physical_idle_query) {
  assert_transmit(DEVICE_GET(fake_uart_cdc));
}

ZTEST(wirelink_uart_irq_adapter, test_missing_fifo_read_and_fill) {
  wl_zephyr_uart_irq_stop(&adapter);
  initialize_link();
  const wl_zephyr_uart_irq_config_t config = {.uart = DEVICE_GET(fake_uart_missing), .link = &link};
  zassert_equal(wl_zephyr_uart_irq_init(&adapter, &config), WL_ERR_NOT_SUPPORTED);
}

ZTEST(wirelink_uart_irq_adapter, test_zero_fifo_returns_leave_isr) {
  const struct device *device = DEVICE_GET(fake_uart_full);
  initialize_adapter(device);
  full_data.read_zero = true;
  full_data.rx_length = 1U;
  fire(device);
  zassert_equal(full_data.reads, 1U);
  full_data.rx_length = 0U;
  full_data.fill_zero = true;
  zassert_ok(wl_send_unreliable(&link, 7U, NULL, 0U));
  fire(device);
  zassert_equal(full_data.fills, 1U);
  zassert_false(wl_zephyr_uart_irq_readiness(&adapter, &link, 0U).runnable);
  full_data.fill_zero = false;
  fire(device);
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 1U));
}

ZTEST(wirelink_uart_irq_adapter, test_consumed_before_drained_and_deadline) {
  const struct device *device = DEVICE_GET(fake_uart_full);
  wl_zephyr_uart_irq_stop(&adapter);
  initialize_link();
  memset(&full_data, 0, sizeof(full_data));
  const wl_zephyr_uart_irq_config_t config = {.uart = device, .link = &link,
    .wait_for_tx_idle = true, .drain_check_ms = 2U};
  zassert_ok(wl_zephyr_uart_irq_init(&adapter, &config));
  zassert_ok(wl_zephyr_uart_irq_start(&adapter));
  wl_tx_handle_t handle;
  zassert_ok(wl_send_reliable(&link, 7U, NULL, 0U, 0U, &handle));
  wl_io_token_t token = adapter.tx_token;
  fire(device);
  zassert_is_null(adapter.tx_data);
  zassert_false(full_data.tx_enabled);
  wl_event_t event;
  zassert_equal(wl_poll(&link, 1U, &event), WL_ERR_NO_DATA);
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 1U));
  zassert_equal(wl_zephyr_uart_irq_readiness(&adapter, &link, 1U).next_deadline_ms, 2U);
  zassert_equal(wl_poll(&link, 2U, &event), WL_ERR_NO_DATA);
  zassert_equal(wl_zephyr_uart_irq_service(&adapter, 2U), WL_ERR_NO_DATA);
  wl_zephyr_uart_irq_stats_t stats;
  wl_zephyr_uart_irq_get_stats(&adapter, &stats);
  zassert_equal(stats.tx_consumed, 1U);
  zassert_equal(stats.tx_drained, 0U);
  zassert_equal(stats.drain_queries, 1U);
  zassert_equal(wl_tx_drained(&link, token + 1U, WL_OK), WL_ERR_NOT_FOUND);
  full_data.line_idle = 1;
  zassert_equal(wl_poll(&link, 3U, &event), WL_ERR_NO_DATA);
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 3U));
  wl_zephyr_uart_irq_get_stats(&adapter, &stats);
  zassert_equal(stats.tx_drained, 1U);
  zassert_equal(stats.drain_queries, 2U);
  zassert_equal(wl_tx_drained(&link, token, WL_OK), WL_ERR_NOT_FOUND);
}

ZTEST(wirelink_uart_irq_adapter, test_negative_physical_query_fails_once) {
  const int failures[] = {-ENOSYS, -EIO};
  for (size_t i = 0U; i < ARRAY_SIZE(failures); ++i) {
    wl_zephyr_uart_irq_stop(&adapter);
    initialize_link();
    memset(&full_data, 0, sizeof(full_data));
    const struct device *device = DEVICE_GET(fake_uart_full);
    const wl_zephyr_uart_irq_config_t config = {
        .uart = device, .link = &link, .wait_for_tx_idle = true};
    zassert_ok(wl_zephyr_uart_irq_init(&adapter, &config));
    zassert_ok(wl_zephyr_uart_irq_start(&adapter));
    wl_tx_handle_t handle;
    zassert_ok(wl_send_reliable(&link, 7U, NULL, 0U, 0U, &handle));
    const wl_io_token_t token = adapter.tx_token;
    fire(device);
    full_data.line_idle = failures[i];
    wl_event_t event;
    zassert_equal(wl_poll(&link, 10U, &event), WL_ERR_NO_DATA);
    zassert_ok(wl_zephyr_uart_irq_service(&adapter, 10U));
    wl_zephyr_uart_irq_stats_t stats;
    wl_zephyr_uart_irq_get_stats(&adapter, &stats);
    zassert_equal(stats.tx_consumed, 1U);
    zassert_equal(stats.tx_drained, 0U);
    zassert_equal(stats.errors, 1U);
    zassert_false(stats.tx_active);
    zassert_ok(wl_poll(&link, 10U, &event));
    zassert_equal(event.type, WL_EVT_TX_FAILED);
    zassert_equal(event.handle, handle);
    wl_tx_result_t result;
    zassert_ok(wl_tx_take(&link, handle, &result));
    zassert_equal(result.result, WL_ERR_IO);
    zassert_equal(wl_tx_drained(&link, token, WL_OK), WL_ERR_NOT_FOUND);
    zassert_equal(wl_tx_consumed(&link, token, WL_OK), WL_ERR_NOT_FOUND);
    zassert_equal(wl_poll(&link, 10U, &event), WL_ERR_NO_DATA);
  }
}

ZTEST(wirelink_uart_irq_adapter, test_latched_wait_and_late_callback_after_stop) {
  const struct device *device = DEVICE_GET(fake_uart_full);
  initialize_adapter(device);
  wl_readiness_t before;
  zassert_ok(wl_link_get_readiness(&link, 0U, &before));
  zassert_false(before.runnable);
  zassert_false(wl_zephyr_uart_irq_readiness(&adapter, &link, 0U).runnable);
  full_data.rx[0] = 0x33U;
  full_data.rx_length = 1U; /* Partial frame does not become runnable. */
  fire(device); /* Publish between a readiness check and waiter entry. */
  wl_readiness_t readiness;
  zassert_ok(wl_link_get_readiness(&link, 0U, &readiness));
  zassert_false(readiness.runnable);
  zassert_ok(wl_zephyr_uart_irq_wait(&adapter, K_NO_WAIT));
  zassert_equal(wl_zephyr_uart_irq_wait(&adapter, K_NO_WAIT), WL_ERR_NO_DATA);
  uart_irq_callback_user_data_t callback = full_data.callback;
  void *context = full_data.user_data;
  wl_zephyr_uart_irq_stop(&adapter);
  memset(&adapter, 0xA5, sizeof(adapter));
  callback(device, context); /* Stable binding must not dereference retired memory. */
  memset(&adapter, 0, sizeof(adapter));
}

ZTEST(wirelink_uart_irq_adapter, test_drain_deadline_wrap_does_not_spin) {
  const struct device *device = DEVICE_GET(fake_uart_full);
  initialize_adapter(device);
  adapter.wait_for_tx_idle = true;
  adapter.drain_check_ms = 3U;
  zassert_ok(wl_set_tx_drain_required(&link, true));
  full_data.line_idle = 0;
  zassert_ok(wl_send_unreliable(&link, 7U, NULL, 0U));
  fire(device);
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, UINT32_MAX - 1U));
  for (unsigned i = 0U; i < 3U; ++i) {
    wl_readiness_t ready = wl_zephyr_uart_irq_readiness(&adapter, &link, UINT32_MAX);
    zassert_false(ready.runnable);
    zassert_equal(ready.next_deadline_ms, 2U);
    zassert_equal(wl_zephyr_uart_irq_service(&adapter, UINT32_MAX), WL_ERR_NO_DATA);
  }
  wl_zephyr_uart_irq_stats_t stats;
  wl_zephyr_uart_irq_get_stats(&adapter, &stats);
  zassert_equal(stats.drain_queries, 1U);
  full_data.line_idle = 1;
  zassert_true(wl_zephyr_uart_irq_readiness(&adapter, &link, 1U).runnable);
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 1U));
  wl_zephyr_uart_irq_get_stats(&adapter, &stats);
  zassert_equal(stats.drain_queries, 2U);
  zassert_equal(stats.tx_drained, 1U);
  zassert_equal(wl_zephyr_uart_irq_readiness(&adapter, &link, 1U).next_deadline_ms,
      WL_NO_DEADLINE);
}

ZTEST(wirelink_uart_irq_adapter, test_missing_callback_and_invalid_setup) {
  wl_zephyr_uart_irq_stop(&adapter);
  initialize_link();
  wl_zephyr_uart_irq_config_t config = {.uart = DEVICE_GET(fake_uart_no_callback), .link = &link};
  zassert_equal(wl_zephyr_uart_irq_init(&adapter, &config), WL_ERR_NOT_SUPPORTED);
  zassert_equal(wl_zephyr_uart_irq_init(NULL, &config), WL_ERR_INVALID_ARG);
  zassert_equal(wl_zephyr_uart_irq_init(&adapter, NULL), WL_ERR_INVALID_ARG);
}
ZTEST(wirelink_uart_irq_adapter, test_late_callback_cannot_target_reused_binding) {
  const struct device *device = DEVICE_GET(fake_uart_full);
  initialize_adapter(device);
  uart_irq_callback_user_data_t callback = full_data.callback;
  void *context = full_data.user_data;
  wl_zephyr_uart_irq_stop(&adapter);
  initialize_adapter(device);
  full_data.rx[0] = 0x33U;
  full_data.rx_length = 1U;
  full_data.pending = true;
  callback(device, context);
  zassert_equal(full_data.reads, 0U);
  fire(device);
  zassert_equal(full_data.reads, 1U);
}
ZTEST(wirelink_uart_irq_adapter, test_error_only_interrupt_publishes_and_wakes) {
  const struct device *device = DEVICE_GET(fake_uart_full);
  initialize_adapter(device);
  full_data.error = UART_ERROR_FRAMING;
  fire(device);
  zassert_true(wl_zephyr_uart_irq_readiness(&adapter, &link, 0U).runnable);
  zassert_ok(wl_zephyr_uart_irq_wait(&adapter, K_NO_WAIT));
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 0U));
  zassert_false(wl_zephyr_uart_irq_readiness(&adapter, &link, 0U).runnable);
  wl_zephyr_uart_irq_stats_t stats;
  wl_zephyr_uart_irq_get_stats(&adapter, &stats);
  zassert_equal(stats.errors, 1U);
}

static unsigned notifications;
static void count_notification(void *context) {
  zassert_equal(context, &notifications);
  ++notifications;
}

ZTEST(wirelink_uart_irq_adapter, test_notification_diagnosis_mixed_callback) {
  const struct device *device = DEVICE_GET(fake_uart_full);
  initialize_adapter(device);
  notifications = 0U;
  adapter.notify = count_notification;
  adapter.notify_user_data = &notifications;
  /* Error, partial RX and consumed TX are three publications in one callback.
   * No decoder or whole-frame condition participates in notification. */
  full_data.error = UART_ERROR_FRAMING;
  full_data.rx[0] = 0x33U;
  full_data.rx_length = 1U;
  zassert_ok(wl_send_unreliable(&link, 7U, NULL, 0U));
  fire(device);
  zassert_equal(notifications, 1U);
  zassert_ok(wl_zephyr_uart_irq_wait(&adapter, K_NO_WAIT));
  zassert_equal(wl_zephyr_uart_irq_wait(&adapter, K_NO_WAIT), WL_ERR_NO_DATA);
#if defined(CONFIG_WIRELINK_ZEPHYR_UART_IRQ_NOTIFICATION_DIAGNOSTICS)
  wl_zephyr_uart_irq_stats_t stats;
  wl_zephyr_uart_irq_get_stats(&adapter, &stats);
  printk("P4 mixed publications=%u gives=%u waits=%u immediate=%u\n",
      stats.publications, stats.callback_gives, stats.waits, stats.wait_ready);
  zassert_equal(stats.publications, 3U);
  zassert_equal(stats.callback_gives, notifications);
#endif
}

ZTEST(wirelink_uart_irq_adapter, test_error_without_pending_irq_still_notifies) {
  const struct device *device = DEVICE_GET(fake_uart_full);
  initialize_adapter(device);
  notifications = 0U;
  adapter.notify = count_notification;
  adapter.notify_user_data = &notifications;
  full_data.error = UART_ERROR_FRAMING;
  full_data.callback(device, full_data.user_data); /* is_pending returns false. */
  zassert_equal(notifications, 1U);
  zassert_ok(wl_zephyr_uart_irq_wait(&adapter, K_NO_WAIT));
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 0U));
}

ZTEST(wirelink_uart_irq_adapter, test_fifo_read_failure_notifies_then_recovers) {
  const struct device *device = DEVICE_GET(fake_uart_full);
  initialize_adapter(device);
  full_data.rx[0] = 0x33U;
  full_data.rx_length = 1U;
  full_data.read_error = -EIO;
  fire(device);
  zassert_ok(wl_zephyr_uart_irq_wait(&adapter, K_NO_WAIT));
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 0U));
  zassert_equal(full_data.rx_offset, 0U);
  full_data.read_error = 0;
  fire(device);
  zassert_equal(full_data.rx_offset, 1U);
  zassert_ok(wl_zephyr_uart_irq_wait(&adapter, K_NO_WAIT));
}

ZTEST(wirelink_uart_irq_adapter, test_fifo_fill_failure_and_rx_share_one_notification) {
  const struct device *device = DEVICE_GET(fake_uart_full);
  initialize_adapter(device);
  notifications = 0U;
  adapter.notify = count_notification;
  adapter.notify_user_data = &notifications;
  full_data.fill_error = -EIO;
  full_data.rx[0] = 0x33U;
  full_data.rx_length = 1U;
  wl_tx_handle_t handle;
  zassert_ok(wl_send_reliable(&link, 7U, NULL, 0U, 0U, &handle));
  fire(device);
  zassert_equal(notifications, 1U);
  zassert_ok(wl_zephyr_uart_irq_wait(&adapter, K_NO_WAIT));
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 1U));
  wl_event_t event;
  zassert_ok(wl_poll(&link, 1U, &event));
  zassert_equal(event.type, WL_EVT_TX_FAILED);
  wl_tx_result_t result;
  zassert_ok(wl_tx_take(&link, handle, &result));
  zassert_equal(result.result, WL_ERR_IO);
  zassert_equal(wl_poll(&link, 1U, &event), WL_ERR_NO_DATA);
  zassert_equal(wl_zephyr_uart_irq_wait(&adapter, K_NO_WAIT), WL_ERR_NO_DATA);
}

ZTEST(wirelink_uart_irq_adapter, test_partial_fifo_fills_only_notify_on_consumed) {
  const struct device *device = DEVICE_GET(fake_uart_full);
  initialize_adapter(device);
  notifications = 0U;
  adapter.notify = count_notification;
  adapter.notify_user_data = &notifications;
  full_data.fill_chunk = 3U;
  full_data.level_pending = true;
  zassert_ok(wl_send_unreliable(&link, 7U, NULL, 0U));
  fire(device);
  zassert_true(full_data.fills > 1U);
  zassert_equal(notifications, 1U);
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 1U));
  zassert_equal(wl_zephyr_uart_irq_wait(&adapter, K_NO_WAIT), WL_ERR_NO_DATA);
}

ZTEST(wirelink_uart_irq_adapter, test_bounded_level_rx_callback) {
  const struct device *device = DEVICE_GET(fake_uart_full);
  initialize_adapter(device);
  notifications = 0U;
  adapter.notify = count_notification;
  adapter.notify_user_data = &notifications;
  full_data.level_pending = true;
  full_data.read_chunk = 1U;
  full_data.rx_length = 40U;
  memset(full_data.rx, 0x33, full_data.rx_length);
  fire(device);
  zassert_equal(full_data.reads, 32U); /* Bound holds under a persistent source. */
  zassert_equal(full_data.rx_offset, 32U);
  zassert_equal(notifications, 1U);
  zassert_ok(wl_zephyr_uart_irq_wait(&adapter, K_NO_WAIT));
  fire(device);
  zassert_equal(full_data.rx_offset, 40U);
  zassert_equal(notifications, 2U);
  zassert_ok(wl_zephyr_uart_irq_wait(&adapter, K_NO_WAIT));
  zassert_equal(wl_zephyr_uart_irq_wait(&adapter, K_NO_WAIT), WL_ERR_NO_DATA);
}

ZTEST(wirelink_uart_irq_adapter, test_partial_rx_and_external_notify_coalesce_without_spin) {
  const struct device *device = DEVICE_GET(fake_uart_full);
  initialize_adapter(device);
  wl_waiter_t waiter;
  zassert_not_null(wl_zephyr_uart_irq_waiter(&adapter, &waiter));
  for (unsigned i = 0U; i < 4U; ++i) {
    full_data.rx[full_data.rx_length++] = 0x33U;
    fire(device);
    waiter.notify(waiter.user_data); /* A synchronized external queue's wake. */
  }
  wl_pump_result_t result;
  const wl_pump_hooks_t hooks = {.adapter_user_data = &adapter,
    .adapter_readiness = wl_zephyr_uart_irq_readiness};
  zassert_ok(wl_pump_step(&link, 0U, 64U, &hooks, &result));
  zassert_equal(result.polls, 0U);
  zassert_false(result.did_work);
  /* RX-only work need not invoke adapter service. Its old latch is finite. */
  zassert_ok(waiter.wait(waiter.user_data, 0U));
  for (unsigned i = 0U; i < 3U; ++i) {
    zassert_ok(wl_pump_step(&link, 0U, 64U, &hooks, &result));
    zassert_false(result.did_work);
    zassert_equal(waiter.wait(waiter.user_data, 0U), WL_ERR_NO_DATA);
  }
  /* Re-arming comes from taking the semaphore, without resetting it. */
  fire(device); /* No new byte; no wake. */
  zassert_equal(waiter.wait(waiter.user_data, 0U), WL_ERR_NO_DATA);
  full_data.rx[full_data.rx_length++] = 0x33U;
  fire(device);
  zassert_ok(waiter.wait(waiter.user_data, 0U));
}

static void publish_after_ack(void) {
  full_data.on_complete = NULL;
  full_data.rx[0] = 0x33U;
  full_data.rx_length = 1U;
  fire(DEVICE_GET(fake_uart_full));
}
ZTEST(wirelink_uart_irq_adapter, test_publication_during_service_after_latch_ack) {
  const struct device *device = DEVICE_GET(fake_uart_full);
  initialize_adapter(device);
  adapter.wait_for_tx_idle = true;
  zassert_ok(wl_set_tx_drain_required(&link, true));
  zassert_ok(wl_send_unreliable(&link, 7U, NULL, 0U));
  fire(device); /* Old completion latch. */
  full_data.on_complete = publish_after_ack;
  zassert_ok(wl_zephyr_uart_irq_service(&adapter, 10U));
  zassert_equal(full_data.rx_offset, 1U);
  zassert_ok(wl_zephyr_uart_irq_wait(&adapter, K_NO_WAIT));
  zassert_equal(wl_zephyr_uart_irq_wait(&adapter, K_NO_WAIT), WL_ERR_NO_DATA);
}

static struct k_thread waiting_owner;
K_THREAD_STACK_DEFINE(waiting_stack, 2048);
static wl_err_t wait_outcome;
static void wait_until_activity(void *a, void *b, void *c) {
  (void)a; (void)b; (void)c;
  wait_outcome = wl_zephyr_uart_irq_wait(&adapter, K_FOREVER);
}
static void block_owner(void) {
  wait_outcome = WL_ERR_INVALID_STATE;
  /* A cooperative thread runs through waiter entry until it blocks, before
   * returning control to this test. No sleep-based scheduling guess. */
  k_thread_create(&waiting_owner, waiting_stack, K_THREAD_STACK_SIZEOF(waiting_stack),
      wait_until_activity, NULL, NULL, NULL, K_PRIO_COOP(0), 0, K_NO_WAIT);
  zassert_equal(wait_outcome, WL_ERR_INVALID_STATE);
}
ZTEST(wirelink_uart_irq_adapter, test_publication_unblocks_waiter) {
  const struct device *device = DEVICE_GET(fake_uart_full);
  initialize_adapter(device);
  block_owner();
  full_data.rx[0] = 0x33U;
  full_data.rx_length = 1U;
  fire(device);
  zassert_ok(k_thread_join(&waiting_owner, K_SECONDS(1)));
  zassert_equal(wait_outcome, WL_OK);
}
ZTEST(wirelink_uart_irq_adapter, test_stop_unblocks_waiter) {
  initialize_adapter(DEVICE_GET(fake_uart_full));
  block_owner();
  wl_zephyr_uart_irq_stop(&adapter);
  zassert_ok(k_thread_join(&waiting_owner, K_SECONDS(1)));
  zassert_equal(wait_outcome, WL_ERR_CANCELLED);
}

#if defined(CONFIG_SMP)
static atomic_t callback_entered, stop_requested, notified_before_stop, callback_spin_timeout;
static struct k_thread producer_thread, stopper_thread;
K_THREAD_STACK_DEFINE(producer_stack, 2048);
K_THREAD_STACK_DEFINE(stopper_stack, 2048);
static void hold_callback_for_stop(void) {
  atomic_set(&callback_entered, 1);
  /* The fake FIFO is inside callback_lock. Only atomic synchronization here:
   * the other CPU requests stop while this callback still owns the binding. */
  for (unsigned i = 0U; i < 10000000U; ++i) {
    if (atomic_get(&stop_requested)) return;
    arch_nop();
  }
  atomic_set(&callback_spin_timeout, 1);
}
static void record_smp_notification(void *context) {
  (void)context;
  atomic_set(&notified_before_stop, 1);
}
static void smp_produce(void *a, void *b, void *c) {
  (void)a; (void)b; (void)c;
  fire(DEVICE_GET(fake_uart_full));
}
static void smp_stop(void *a, void *b, void *c) {
  (void)a; (void)b; (void)c;
  while (!atomic_get(&callback_entered)) k_yield();
  atomic_set(&stop_requested, 1);
  wl_zephyr_uart_irq_stop(&adapter);
}
ZTEST(wirelink_uart_irq_adapter, test_smp_stop_joins_publication_and_notifier) {
  const struct device *device = DEVICE_GET(fake_uart_full);
  initialize_adapter(device);
  atomic_clear(&callback_entered);
  atomic_clear(&stop_requested);
  atomic_clear(&notified_before_stop);
  atomic_clear(&callback_spin_timeout);
  full_data.on_read = hold_callback_for_stop;
  full_data.rx[0] = 0x33U;
  full_data.rx_length = 1U;
  adapter.notify = record_smp_notification;
  uart_irq_callback_user_data_t callback = full_data.callback;
  void *generation = full_data.user_data;
  k_thread_create(&producer_thread, producer_stack, K_THREAD_STACK_SIZEOF(producer_stack),
      smp_produce, NULL, NULL, NULL, K_PRIO_COOP(0), 0, K_FOREVER);
  k_thread_create(&stopper_thread, stopper_stack, K_THREAD_STACK_SIZEOF(stopper_stack),
      smp_stop, NULL, NULL, NULL, K_PRIO_COOP(0), 0, K_FOREVER);
  zassert_ok(k_thread_cpu_pin(&producer_thread, 1));
  zassert_ok(k_thread_cpu_pin(&stopper_thread, 0));
  k_thread_start(&producer_thread);
  k_thread_start(&stopper_thread);
  zassert_ok(k_thread_join(&stopper_thread, K_SECONDS(2)));
  zassert_ok(k_thread_join(&producer_thread, K_SECONDS(2)));
  zassert_equal(atomic_get(&callback_spin_timeout), 0);
  zassert_equal(atomic_get(&notified_before_stop), 1);
  wl_zephyr_uart_irq_stats_t stats;
  wl_zephyr_uart_irq_get_stats(&adapter, &stats);
  zassert_equal(stats.rx_bytes, 1U);
  zassert_false(stats.started);
  full_data.on_read = NULL;
  callback(device, generation); /* Stop retired the binding after notification. */
  zassert_equal(full_data.reads, 1U);
}
#endif

static void cleanup(void *fixture) {
  (void)fixture;
  inspect_timeout = false;
  on_wait_entry = NULL;
  wl_zephyr_uart_irq_stop(&adapter);
}
#if defined(CONFIG_WIRELINK_ZEPHYR_UART_IRQ_TIMING)
ZTEST(wirelink_uart_irq_adapter, test_callback_probe_counts_entries_and_retired_binding) {
  const struct device *device = DEVICE_GET(fake_uart_full);
  initialize_adapter(device);
  /* An entry with no ready source still counts once, without an RX attempt. */
  fire(device);
  fire(device);
  wl_zephyr_uart_irq_stats_t before, after;
  wl_zephyr_uart_irq_get_stats(&adapter, &before);
  zassert_equal(before.callback_irqs, 2U);
  zassert_equal(before.rx_irqs, 0U);
  uart_irq_callback_user_data_t callback = full_data.callback;
  void *context = full_data.user_data;
  wl_zephyr_uart_irq_stop(&adapter);
  callback(device, context);
  wl_zephyr_uart_irq_get_stats(&adapter, &after);
  zassert_equal(after.callback_irqs, before.callback_irqs);
  zassert_equal(after.callback_ticks, before.callback_ticks);
}
#endif

static wl_endpoint_t ready_endpoint;
static wl_readiness_t ready_hint;
static unsigned ready_action, ready_reads;
static wl_time_ms_t ready_time, ready_queried_at;
static wl_time_ms_t ready_clock(void *context) {
  (void)context;
  ++ready_reads;
  return ready_time;
}
static wl_readiness_t ready_application(const void *context, const wl_ctx_t *ctx,
    wl_time_ms_t now) {
  (void)context; (void)ctx;
  ready_queried_at = now;
  unsigned action = ready_action;
  ready_action = 0U;
  if (action == 1U) {
    full_data.rx[0] = 0x33U;
    full_data.rx_length = 1U;
    fire(DEVICE_GET(fake_uart_full));
  } else if (action == 2U) ready_time += 7U; /* Query cost, including clock wrap. */
  else if (action == 3U) wl_zephyr_uart_irq_stop(&adapter);
  else if (action == 4U) {
    const wl_waiter_t *waiter = wl_endpoint_waiter(&ready_endpoint);
    waiter->notify(waiter->user_data);
  }
  return ready_hint;
}
static void initialize_ready_endpoint(void) {
  wl_zephyr_uart_irq_stop(&adapter);
  memset(&ready_endpoint, 0, sizeof(ready_endpoint));
  memset(&full_data, 0, sizeof(full_data));
  full_data.line_idle = 1;
  ready_hint = wl_readiness_idle();
  ready_action = ready_reads = 0U;
  ready_time = UINT32_MAX - 4U;
  ready_queried_at = 0U;
  on_wait_entry = NULL;
  const wl_config_t config = {.envelope = WL_ENVELOPE_COBS_STREAM,
    .integrity = WL_INTEGRITY_CRC16, .max_payload_len = sizeof(tx_payload), .session_id = 1U};
  const wl_storage_t storage = {.tx_payload = tx_payload, .tx_payload_size = sizeof(tx_payload),
    .tx_unit = tx_unit, .tx_unit_size = sizeof(tx_unit), .control_unit = control,
    .control_unit_size = sizeof(control), .rx_fallback = fallback,
    .rx_fallback_size = sizeof(fallback), .rx_fifo = fifo, .rx_fifo_size = sizeof(fifo)};
  const wl_clock_t clock = {ready_clock, NULL};
  const wl_pump_hooks_t app = {.application_readiness = ready_application};
  zassert_ok(wl_endpoint_init(&ready_endpoint, &config, &storage, &clock, &app));
  const wl_zephyr_uart_irq_config_t port = {.uart = DEVICE_GET(fake_uart_full)};
  zassert_ok(wl_zephyr_uart_irq_attach(&adapter, &ready_endpoint, &port));
  zassert_ok(wl_zephyr_uart_irq_start(&adapter));
}
ZTEST(wirelink_uart_irq_adapter, test_cdc_owner_wait_budget_and_query_delay) {
  initialize_ready_endpoint();
  inspect_timeout = true; /* Observe the real adapter wait conversion. */
  inspected_waits = 0U;
  ready_hint = wl_readiness_at(10U);
  ready_action = 2U;
  zassert_equal(cdc_wait_for_activity(&ready_endpoint), WL_ERR_NO_DATA);
  zassert_true(inspected_waits > 0U);
  zassert_equal(ready_queried_at, UINT32_MAX - 4U);
  zassert_equal(ready_time, 2U);
  zassert_equal(ready_reads, 2U);
  zassert_true(K_TIMEOUT_EQ(inspected_timeout, K_MSEC(3)));
  ready_reads = 0U;
  ready_hint = wl_readiness_at(6U);
  ready_action = 2U; /* A seven-ms query exhausts this protocol budget. */
  zassert_equal(cdc_wait_for_activity(&ready_endpoint), WL_ERR_NO_DATA);
  zassert_true(K_TIMEOUT_EQ(inspected_timeout, K_NO_WAIT));
  zassert_equal(ready_reads, 2U);
  ready_hint = wl_readiness_at(UINT32_MAX - 1U);
  zassert_equal(cdc_wait_for_activity(&ready_endpoint), WL_ERR_NO_DATA);
  zassert_false(K_TIMEOUT_EQ(inspected_timeout, K_FOREVER));
#if !defined(CONFIG_TIMEOUT_64BIT)
  zassert_true(inspected_timeout.ticks <= INT32_MAX);
#endif
  ready_reads = 0U;
  ready_hint = wl_readiness_idle();
  zassert_equal(cdc_wait_for_activity(&ready_endpoint), WL_ERR_NO_DATA);
  zassert_true(K_TIMEOUT_EQ(inspected_timeout, K_FOREVER));
  zassert_equal(ready_reads, 1U); /* Infinity needs no second clock read. */
  ready_reads = 0U;
  ready_hint = wl_readiness_at(0U);
  zassert_equal(cdc_wait_for_activity(&ready_endpoint), WL_ERR_NO_DATA);
  zassert_true(K_TIMEOUT_EQ(inspected_timeout, K_NO_WAIT));
  zassert_equal(ready_reads, 1U);
  ready_hint = wl_readiness_idle();
  ready_hint.runnable = 1U;
  zassert_equal(cdc_wait_for_activity(&ready_endpoint), WL_ERR_NO_DATA);
  zassert_true(K_TIMEOUT_EQ(inspected_timeout, K_NO_WAIT));
  inspect_timeout = false;
  wl_endpoint_close(&ready_endpoint);
}

static void publish_at_wait_entry(void) {
  full_data.rx[0] = 0x33U;
  full_data.rx_length = 1U;
  fire(DEVICE_GET(fake_uart_full));
}
static void stop_at_wait_entry(void) { wl_zephyr_uart_irq_stop(&adapter); }
ZTEST(wirelink_uart_irq_adapter, test_cdc_owner_publication_windows) {
  for (unsigned window = 0U; window < 4U; ++window) {
    initialize_ready_endpoint();
    if (window == 0U) publish_at_wait_entry(); /* Before readiness query. */
    else if (window == 1U) ready_action = 1U; /* During application query. */
    else if (window == 2U) on_wait_entry = publish_at_wait_entry;
    else ready_action = 4U; /* External work, without a complete RX frame. */
    zassert_ok(cdc_wait_for_activity(&ready_endpoint));
    zassert_ok(wl_endpoint_step(&ready_endpoint, 8U));
    ready_hint = wl_readiness_at(0U); /* Inspect the latch without blocking. */
    zassert_equal(cdc_wait_for_activity(&ready_endpoint), WL_ERR_NO_DATA);
    wl_endpoint_close(&ready_endpoint);
  }
}

ZTEST(wirelink_uart_irq_adapter, test_cdc_owner_stop_during_query_and_wait_entry) {
  for (unsigned mode = 0U; mode < 4U; ++mode) {
    initialize_ready_endpoint();
    if (mode == 0U) ready_action = 3U; /* Infinite hint. */
    else if (mode == 1U) { ready_action = 3U; ready_hint.runnable = 1U; }
    else if (mode == 2U) { ready_action = 3U; ready_hint = wl_readiness_at(0U); }
    else on_wait_entry = stop_at_wait_entry;
    zassert_equal(cdc_wait_for_activity(&ready_endpoint), WL_ERR_CANCELLED);
    wl_endpoint_close(&ready_endpoint);
  }
}

static void cdc_wait_until_activity(void *a, void *b, void *c) {
  (void)a; (void)b; (void)c;
  wait_outcome = cdc_wait_for_activity(&ready_endpoint);
}
ZTEST(wirelink_uart_irq_adapter, test_cdc_owner_notification_and_stop_unblock) {
  for (unsigned stop = 0U; stop < 2U; ++stop) {
    initialize_ready_endpoint();
    wait_outcome = WL_ERR_INVALID_STATE;
    k_thread_create(&waiting_owner, waiting_stack, K_THREAD_STACK_SIZEOF(waiting_stack),
        cdc_wait_until_activity, NULL, NULL, NULL, K_PRIO_COOP(0), 0, K_NO_WAIT);
    if (stop) wl_zephyr_uart_irq_stop(&adapter);
    else {
      const wl_waiter_t *waiter = wl_endpoint_waiter(&ready_endpoint);
      waiter->notify(waiter->user_data);
    }
    zassert_ok(k_thread_join(&waiting_owner, K_SECONDS(1)));
    zassert_equal(wait_outcome, stop ? WL_ERR_CANCELLED : WL_OK);
    wl_endpoint_close(&ready_endpoint);
  }
}

ZTEST(wirelink_uart_irq_adapter, test_cdc_owner_endpoint_lifetime) {
  zassert_equal(cdc_wait_for_activity(NULL), WL_ERR_INVALID_ARG);
  initialize_ready_endpoint();
  zassert_ok(wl_endpoint_set_waiter(&ready_endpoint, NULL));
  zassert_equal(cdc_wait_for_activity(&ready_endpoint), WL_ERR_NOT_SUPPORTED);
  wl_endpoint_close(&ready_endpoint);
  zassert_equal(cdc_wait_for_activity(&ready_endpoint), WL_ERR_NOT_INITIALIZED);
}

ZTEST_SUITE(wirelink_uart_irq_adapter, NULL, NULL, NULL, cleanup, NULL);

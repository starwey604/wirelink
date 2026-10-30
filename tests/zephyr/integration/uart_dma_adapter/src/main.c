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
#include "wirelink/wirelink.h"
#include "wirelink/zephyr/uart_dma.h"
#include "context.h"

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
static struct k_thread callback_thread;
static struct k_thread quiesce_thread;
static struct device_state fake_state = {
    .init_res = 0,
    .initialized = 1,
};

static int fake_callback_set(const struct device *dev, uart_callback_t callback,
                             void *user_data) {
  struct fake_uart_data *data = dev->data;

  data->callback = callback;
  data->callback_data = user_data;
  data->callback_sets++;
  if (callback == NULL) k_sem_give(&callback_detached);
  return 0;
}

static int fake_tx(const struct device *dev, const uint8_t *buf, size_t len,
                   int32_t timeout) {
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

static int fake_rx_enable(const struct device *dev, uint8_t *buf, size_t len,
                          int32_t timeout) {
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

struct fixture {
  wl_ctx_t link;
  wl_config_t config;
  wl_storage_t storage;
  wl_storage_requirements_t requirements;
  wl_zephyr_uart_dma_t adapter;
  uint8_t tx_payload[TEST_MAX_PAYLOAD];
  uint8_t tx_unit[TEST_STORAGE_SIZE];
  uint8_t control_unit[TEST_STORAGE_SIZE];
  uint8_t rx_fifo[TEST_STORAGE_SIZE];
  uint8_t rx_fallback[TEST_STORAGE_SIZE];
  uint8_t rx_dma_buffers[2][32];
};

static struct fixture fixture;

static void init_fixture(struct fixture *fixture) {
  wl_zephyr_uart_dma_config_t adapter_config;

  memset(&fake_data, 0, sizeof(fake_data));
  fake_data.tx_idle = true;
  memset(fixture, 0, sizeof(*fixture));
  fixture->config = (wl_config_t){
      .max_payload_len = TEST_MAX_PAYLOAD,
      .envelope = WL_ENVELOPE_COBS_STREAM,
      .integrity = WL_INTEGRITY_CRC32C,
      .session_id = UINT64_C(0x123456789ABCDEF0),
      .max_retries = 1U,
      .ack_timeout_ms = 20U,
  };
  zassert_ok(wl_config_requirements(&fixture->config, &fixture->requirements));
  fixture->storage = (wl_storage_t){
      .tx_payload = fixture->tx_payload,
      .tx_payload_size = fixture->requirements.tx_payload_size,
      .tx_unit = fixture->tx_unit,
      .tx_unit_size = fixture->requirements.tx_unit_size,
      .control_unit = fixture->control_unit,
      .control_unit_size = fixture->requirements.control_unit_size,
      .rx_fifo = fixture->rx_fifo,
      .rx_fifo_size = fixture->requirements.rx_fifo_size,
      .rx_fallback = fixture->rx_fallback,
      .rx_fallback_size = fixture->requirements.rx_fallback_size,
  };
  zassert_true(fixture->storage.tx_payload_size <= sizeof(fixture->tx_payload));
  zassert_true(fixture->storage.tx_unit_size <= sizeof(fixture->tx_unit));
  zassert_true(fixture->storage.control_unit_size <=
               sizeof(fixture->control_unit));
  zassert_true(fixture->storage.rx_fifo_size <= sizeof(fixture->rx_fifo));
  zassert_true(fixture->storage.rx_fallback_size <=
               sizeof(fixture->rx_fallback));
  zassert_ok(wl_init(&fixture->link, &fixture->config, &fixture->storage));

  adapter_config = (wl_zephyr_uart_dma_config_t){
      .uart = &fake_uart,
      .link = &fixture->link,
      .rx_buffers = {
          {fixture->rx_dma_buffers[0], sizeof(fixture->rx_dma_buffers[0])},
          {fixture->rx_dma_buffers[1], sizeof(fixture->rx_dma_buffers[1])},
      },
      .timeout_us = 100,
      .tx_timeout_us = SYS_FOREVER_US,
      .wait_for_tx_idle = true,
  };
  zassert_ok(wl_zephyr_uart_dma_init(&fixture->adapter, &adapter_config));
  zassert_ok(wl_zephyr_uart_dma_start(&fixture->adapter));
}

static void emit_tx_event(enum uart_event_type type, size_t length) {
  struct uart_event event = {
      .type = type,
      .data.tx =
          {
              .buf = fake_data.tx_buf,
              .len = length,
          },
  };

  fake_data.tx_active = false;
  fake_data.tx_idle = true;
  fake_data.callback(&fake_uart, &event, fake_data.callback_data);
}

static void emit_tx_done_before_idle(void) {
  struct uart_event event = {
      .type = UART_TX_DONE,
      .data.tx =
          {
              .buf = fake_data.tx_buf,
              .len = fake_data.tx_len,
          },
  };

  fake_data.tx_active = false;
  fake_data.callback(&fake_uart, &event, fake_data.callback_data);
}

static void emit_rx_ready(size_t offset, size_t length) {
  struct uart_event event = {
      .type = UART_RX_RDY,
      .data.rx =
          {
              .buf = fake_data.rx_buf,
              .offset = offset,
              .len = length,
          },
  };

  fake_data.callback(&fake_uart, &event, fake_data.callback_data);
}

static void emit_rx_disabled(void) {
  struct uart_event event = {.type = UART_RX_DISABLED};

  fake_data.rx_enabled = false;
  fake_data.rx_buf = NULL;
  fake_data.rx_len = 0U;
  fake_data.rx_next_buf = NULL;
  fake_data.rx_next_len = 0U;
  fake_data.rx_disabled_events++;
  fake_data.callback(&fake_uart, &event, fake_data.callback_data);
}

static void release_current_and_advance(void) {
  struct uart_event released = {
      .type = UART_RX_BUF_RELEASED,
      .data.rx_buf = {.buf = fake_data.rx_buf},
  };
  struct uart_event requested = {.type = UART_RX_BUF_REQUEST};

  fake_data.callback(&fake_uart, &released, fake_data.callback_data);
  fake_data.rx_buf = fake_data.rx_next_buf;
  fake_data.rx_len = fake_data.rx_next_len;
  fake_data.rx_next_buf = NULL;
  fake_data.rx_next_len = 0U;
  if (fake_data.rx_buf != NULL) {
    fake_data.callback(&fake_uart, &requested, fake_data.callback_data);
  } else {
    emit_rx_disabled();
  }
}

static void release_all_and_disable(void) {
  struct uart_event released = {.type = UART_RX_BUF_RELEASED};

  if (fake_data.rx_buf != NULL) {
    released.data.rx_buf.buf = fake_data.rx_buf;
    fake_data.callback(&fake_uart, &released, fake_data.callback_data);
  }
  if (fake_data.rx_next_buf != NULL) {
    released.data.rx_buf.buf = fake_data.rx_next_buf;
    fake_data.callback(&fake_uart, &released, fake_data.callback_data);
  }
  emit_rx_disabled();
}

static void receive_and_release(const uint8_t *data, size_t length) {
  zassert_not_null(fake_data.rx_buf);
  zassert_true(length <= fake_data.rx_len);
  memcpy(fake_data.rx_buf, data, length);
  emit_rx_ready(0U, length);
  release_current_and_advance();
}

static size_t drain_rx_events(struct fixture *fixture,
                              const uint8_t *expected_payload,
                              size_t expected_length, wl_time_ms_t *now) {
  size_t count = 0U;

  for (;;) {
    wl_event_t event = {0};
    int ret = wl_poll(&fixture->link, (*now)++, &event);

    if (ret == WL_ERR_NO_DATA) {
      return count;
    }
    zassert_ok(ret);
    if (event.type == WL_EVT_UNRELIABLE_RX ||
        event.type == WL_EVT_RELIABLE_RX) {
      zassert_equal(event.payload_len, expected_length);
      zassert_mem_equal(event.payload, expected_payload, expected_length);
      ++count;
    }
    wl_event_release(&fixture->link, &event);
  }
}

static size_t encode_rx_frame(const struct fixture *fixture,
                              const uint8_t *payload, size_t payload_len,
                              uint8_t *output, size_t output_size) {
  wl_wire_packet_t packet = {
      .type = WL_PACKET_DATA,
      .integrity = fixture->config.integrity,
      .message_id = 0x301U,
      .session_id = UINT64_C(0x0FEDCBA987654321),
      .sequence = 1U,
      .payload = payload,
      .payload_len = payload_len,
  };
  size_t output_len = 0U;

  zassert_ok(wl_frame_encode(&packet, fixture->config.envelope, output,
                             output_size, &output_len));
  return output_len;
}

ZTEST(wirelink_uart_dma_adapter, test_tx_done_is_deferred_to_service) {
  const uint8_t payload[] = {0x11U, 0x22U, 0x33U};
  wl_zephyr_uart_dma_stats_t stats = {0};
  wl_event_t event = {0};

  init_fixture(&fixture);
  zassert_equal(fake_data.callback_sets, 1U);
  zassert_equal(wl_ctx_impl(&fixture.link)->sink_user_data, &fixture.adapter);
  zassert_ok(
      wl_send_unreliable(&fixture.link, 0x101U, payload, sizeof(payload)));
  zassert_true(fake_data.tx_active);
  zassert_equal(wl_ctx_impl(&fixture.link)->tx_inflight, 1U);

  emit_tx_event(UART_TX_DONE, fake_data.tx_len);
  zassert_equal(wl_ctx_impl(&fixture.link)->tx_inflight, 1U,
                "UART callback must not enter the protocol consumer");
  wl_zephyr_uart_dma_get_stats(&fixture.adapter, &stats);
  zassert_equal(stats.tx_active, 1U);
  zassert_equal(stats.tx_done_events, 1U);

  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter, k_uptime_get_32()));
  zassert_equal(wl_ctx_impl(&fixture.link)->tx_inflight, 0U);
  zassert_ok(wl_poll(&fixture.link, 1U, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
}

ZTEST(wirelink_uart_dma_adapter, test_tx_done_waits_for_physical_idle) {
  const uint8_t payload[] = {0x31U, 0x32U};
  wl_event_t event = {0};

  init_fixture(&fixture);
  zassert_ok(
      wl_send_unreliable(&fixture.link, 0x108U, payload, sizeof(payload)));
  emit_tx_done_before_idle();
  wl_time_ms_t now = k_uptime_get_32();
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter, now));
  zassert_equal(wl_ctx_impl(&fixture.link)->tx_inflight, 0U);
  zassert_equal(wl_ctx_impl(&fixture.link)->tx_drain_pending, 1U);
  wl_readiness_t ready = wl_zephyr_uart_dma_readiness(&fixture.adapter, &fixture.link, now);
  zassert_false(ready.runnable);
  zassert_equal(ready.next_deadline_ms, 1U);

  fake_data.tx_idle = true;
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter, now + 1U));
  zassert_equal(wl_ctx_impl(&fixture.link)->tx_drain_pending, 0U);
  zassert_equal(wl_ctx_impl(&fixture.link)->tx_inflight, 0U);
  zassert_ok(wl_poll(&fixture.link, 1U, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
}

ZTEST(wirelink_uart_dma_adapter, test_tx_busy_retries_and_failure_maps_to_io) {
  const uint8_t payload[] = {0x44U, 0x55U};
  wl_zephyr_uart_dma_stats_t stats = {0};
  wl_event_t event = {0};
  int ret;

  init_fixture(&fixture);
  fake_data.next_tx_result = -EBUSY;
  zassert_ok(
      wl_send_unreliable(&fixture.link, 0x102U, payload, sizeof(payload)));
  zassert_equal(wl_ctx_impl(&fixture.link)->tx_queued, 1U);
  zassert_equal(wl_poll(&fixture.link, 1U, &event), WL_ERR_NO_DATA);
  zassert_false(fake_data.tx_active);
  zassert_equal(fake_data.tx_calls, 1U);
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter, fixture.adapter.tx_retry_due));
  zassert_equal(wl_poll(&fixture.link, 1U, &event), WL_ERR_NO_DATA);
  zassert_true(fake_data.tx_active);
  emit_tx_event(UART_TX_DONE, fake_data.tx_len);
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter, k_uptime_get_32()));
  zassert_ok(wl_poll(&fixture.link, 2U, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);

  fake_data.next_tx_result = -EIO;
  ret = wl_send_unreliable(&fixture.link, 0x103U, payload, sizeof(payload));
  zassert_equal(ret, WL_ERR_IO, "unexpected send result: %d", ret);
  wl_zephyr_uart_dma_get_stats(&fixture.adapter, &stats);
  zassert_equal(stats.tx_busy, 1U);
  zassert_equal(stats.tx_submissions, 1U);
  zassert_equal(stats.errors, 1U);
}

ZTEST(wirelink_uart_dma_adapter, test_tx_abort_can_retry_from_service) {
  const uint8_t payload[] = {0x66U, 0x77U};
  wl_tx_handle_t handle = 0U;
  wl_tx_state_t state = WL_TX_STATE_IDLE;

  init_fixture(&fixture);
  zassert_ok(wl_send_reliable(&fixture.link, 0x104U, payload, sizeof(payload), 0U,
                              &handle));
  zassert_not_equal(handle, 0U);
  emit_tx_event(UART_TX_ABORTED, 1U);
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter, k_uptime_get_32()));
  zassert_equal(fake_data.tx_calls, 2U,
                "completion service must permit synchronous retry submit");
  zassert_true(fake_data.tx_active);

  emit_tx_event(UART_TX_DONE, fake_data.tx_len);
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter, k_uptime_get_32()));
  zassert_ok(wl_tx_status(&fixture.link, handle, &state));
  zassert_equal(state, WL_TX_STATE_WAITING_ACK);
}

ZTEST(wirelink_uart_dma_adapter, test_rx_release_restarts_and_decodes) {
  const uint8_t payload[] = {0xA1U, 0x00U, 0xB2U, 0xC3U};
  uint8_t wire[TEST_STORAGE_SIZE];
  wl_event_t event = {0};
  size_t wire_len;

  init_fixture(&fixture);
  wire_len =
      encode_rx_frame(&fixture, payload, sizeof(payload), wire, sizeof(wire));
  zassert_true(wire_len <= fake_data.rx_len);
  memcpy(fake_data.rx_buf, wire, wire_len);
  emit_rx_ready(0U, wire_len);
  release_current_and_advance();

  zassert_ok(wl_poll(&fixture.link, 1U, &event));
  zassert_equal(event.type, WL_EVT_UNRELIABLE_RX);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  wl_event_release(&fixture.link, &event);
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter, k_uptime_get_32()));
  zassert_true(fake_data.rx_enabled);
  zassert_equal(fake_data.rx_enables, 1U);
  zassert_true(fake_data.rx_next_buf != NULL);
}

ZTEST(wirelink_uart_dma_adapter,
      test_rx_short_ping_pong_preserves_continuous_stream) {
  const uint8_t payload[] = {0x21U, 0x00U, 0x43U, 0x65U, 0x87U};
  uint8_t frame[TEST_STORAGE_SIZE];
  uint8_t stream[TEST_STORAGE_SIZE * 4U];
  const size_t frame_count = 12U;
  const size_t chunk_size = 7U;
  wl_rx_counters_t counters = {0};
  wl_zephyr_uart_dma_stats_t stats = {0};
  wl_time_ms_t now = 1U;
  size_t frame_len;
  size_t stream_len;
  size_t received = 0U;

  init_fixture(&fixture);
  frame_len = encode_rx_frame(&fixture, payload, sizeof(payload), frame,
                              sizeof(frame));
  stream_len = frame_len * frame_count;
  zassert_true(stream_len <= sizeof(stream));
  for (size_t i = 0U; i < frame_count; ++i) {
    memcpy(stream + i * frame_len, frame, frame_len);
  }

  for (size_t offset = 0U; offset < stream_len;) {
    size_t length = MIN(chunk_size, stream_len - offset);

    receive_and_release(stream + offset, length);
    offset += length;
    received += drain_rx_events(&fixture, payload, sizeof(payload), &now);
    zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter, k_uptime_get_32()));
  }
  received += drain_rx_events(&fixture, payload, sizeof(payload), &now);

  zassert_equal(received, frame_count);
  zassert_equal(fake_data.rx_enables, 1U);
  zassert_equal(fake_data.rx_disabled_events, 0U);
  zassert_true(fake_data.rx_buf_responses > frame_count);
  zassert_ok(wl_rx_get_counters(&fixture.link, &counters));
  zassert_equal(counters.overflow, 0U);
  zassert_equal(counters.malformed, 0U);
  wl_zephyr_uart_dma_get_stats(&fixture.adapter, &stats);
  zassert_equal(stats.errors, 0U);
}

ZTEST(wirelink_uart_dma_adapter,
      test_rx_staging_survives_temporary_ring_backpressure) {
  uint8_t payload[32];
  uint8_t frame[TEST_STORAGE_SIZE];
  uint8_t stream[TEST_STORAGE_SIZE * 2U];
  const size_t frame_count = 3U;
  wl_rx_counters_t counters = {0};
  wl_zephyr_uart_dma_stats_t stats = {0};
  wl_time_ms_t now = 1U;
  size_t frame_len;
  size_t stream_len;
  size_t offset = 0U;
  size_t received = 0U;

  memset(payload, 0x5AU, sizeof(payload));
  init_fixture(&fixture);
  frame_len = encode_rx_frame(&fixture, payload, sizeof(payload), frame,
                              sizeof(frame));
  stream_len = frame_len * frame_count;
  zassert_true(stream_len <= sizeof(stream));
  for (size_t i = 0U; i < frame_count; ++i) {
    memcpy(stream + i * frame_len, frame, frame_len);
  }

  /* Fill three DMA buffers without polling. The third publication reaches the
   * one-frame RX ring limit and remains retained in its released staging slot. */
  for (size_t i = 0U; i < 3U; ++i) {
    receive_and_release(stream + offset, sizeof(fixture.rx_dma_buffers[0]));
    offset += sizeof(fixture.rx_dma_buffers[0]);
  }
  zassert_is_null(fake_data.rx_next_buf);
  {
    struct uart_event requested = {.type = UART_RX_BUF_REQUEST};

    /* Advancing to another current buffer may repeat an unanswered request.
     * It is the same one-slot demand, not an adapter protocol violation. */
    fake_data.callback(&fake_uart, &requested, fake_data.callback_data);
    wl_zephyr_uart_dma_get_stats(&fixture.adapter, &stats);
    zassert_equal(stats.errors, 0U);
  }

  received += drain_rx_events(&fixture, payload, sizeof(payload), &now);
  zassert_true(received > 0U);
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter, k_uptime_get_32()));
  zassert_not_null(fake_data.rx_next_buf,
                   "released staging slot must be resupplied after drain");

  while (offset < stream_len) {
    size_t length = MIN(sizeof(fixture.rx_dma_buffers[0]), stream_len - offset);

    receive_and_release(stream + offset, length);
    offset += length;
    received += drain_rx_events(&fixture, payload, sizeof(payload), &now);
    zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter, k_uptime_get_32()));
  }
  received += drain_rx_events(&fixture, payload, sizeof(payload), &now);

  zassert_equal(received, frame_count);
  zassert_equal(fake_data.rx_disabled_events, 0U);
  zassert_ok(wl_rx_get_counters(&fixture.link, &counters));
  zassert_equal(counters.overflow, 0U);
  zassert_equal(counters.malformed, 0U);
  wl_zephyr_uart_dma_get_stats(&fixture.adapter, &stats);
  zassert_equal(stats.errors, 0U);
}

ZTEST(wirelink_uart_dma_adapter, test_stop_waits_for_driver_and_can_restart) {
  const uint8_t payload[] = {0x88U, 0x99U};
  wl_zephyr_uart_dma_stats_t stats = {0};

  init_fixture(&fixture);
  zassert_ok(
      wl_send_unreliable(&fixture.link, 0x105U, payload, sizeof(payload)));
  zassert_ok(wl_zephyr_uart_dma_stop(&fixture.adapter));
  zassert_equal(fake_data.rx_disables, 1U);
  zassert_equal(fake_data.tx_aborts, 1U);
  zassert_equal(wl_zephyr_uart_dma_service(&fixture.adapter, k_uptime_get_32()),
                WL_ERR_WOULD_BLOCK);

  emit_tx_event(UART_TX_ABORTED, 0U);
  release_all_and_disable();
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter, k_uptime_get_32()));
  wl_zephyr_uart_dma_get_stats(&fixture.adapter, &stats);
  zassert_equal(stats.started, 0U);
  zassert_equal(stats.stopping, 0U);
  zassert_equal(stats.tx_active, 0U);
  zassert_equal(
      wl_send_unreliable(&fixture.link, 0x106U, payload, sizeof(payload)),
      WL_ERR_IO);

  zassert_ok(wl_zephyr_uart_dma_start(&fixture.adapter));
  zassert_true(fake_data.rx_enabled);
  zassert_ok(
      wl_send_unreliable(&fixture.link, 0x107U, payload, sizeof(payload)));
}

static void fill_ring_and_staging(void) {
  uint8_t payload[32];
  uint8_t frame[TEST_STORAGE_SIZE];
  uint8_t stream[TEST_STORAGE_SIZE * 3U];
  memset(payload, 0x5a, sizeof(payload));
  size_t length = encode_rx_frame(&fixture, payload, sizeof(payload), frame,
                                  sizeof(frame));
  for (size_t i = 0U; i < 3U; ++i) memcpy(stream + i * length, frame, length);
  for (size_t i = 0U; i < 3U; ++i)
    receive_and_release(stream + i * sizeof(fixture.rx_dma_buffers[0]),
                         sizeof(fixture.rx_dma_buffers[0]));
  /* No poll has run, so there is no application RX lease. */
  zassert_false(wl_rx_has_space(&fixture.link));
  size_t pending = 0U;
  for (size_t i = 0U; i < 2U; ++i)
    pending += fixture.adapter.slots[i].received - fixture.adapter.slots[i].forwarded;
  zassert_true(pending > 0U);
}

ZTEST(wirelink_uart_dma_adapter, test_stop_discards_staging_with_full_ring) {
  init_fixture(&fixture);
  fill_ring_and_staging();
  atomic_val_t published = atomic_get(&fixture.adapter.published_bytes);
  zassert_ok(wl_zephyr_uart_dma_stop(&fixture.adapter));
  zassert_equal(wl_zephyr_uart_dma_service(&fixture.adapter, 1U), WL_ERR_WOULD_BLOCK);
  release_all_and_disable();
  int ret = wl_zephyr_uart_dma_service(&fixture.adapter, 2U);
  /* Let a broken implementation clean up before reporting its failed stop. */
  if (ret != WL_OK) {
    zassert_ok(wl_feed_recover_reset(&fixture.link));
    (void)wl_zephyr_uart_dma_service(&fixture.adapter, 3U);
  }
  zassert_ok(ret);
  zassert_false(wl_rx_has_space(&fixture.link), "stop must not consume the core ring");
  zassert_equal(atomic_get(&fixture.adapter.published_bytes), published);
  zassert_equal(atomic_get(&fixture.adapter.started), 0);
  zassert_equal(atomic_get(&fixture.adapter.stopping), 0);
  for (size_t i = 0U; i < 2U; ++i) zassert_equal(fixture.adapter.slots[i].order, 0U);
  wl_zephyr_uart_dma_quiesce(&fixture.adapter);
  zassert_true(fixture.adapter.detached);
}

ZTEST(wirelink_uart_dma_adapter, test_disabled_waits_for_queued_buffer_returns) {
  init_fixture(&fixture);
  /* The driver is disabled, but its buffer-return callbacks are still queued. */
  fake_data.rx_enabled = false;
  zassert_ok(wl_zephyr_uart_dma_stop(&fixture.adapter));
  zassert_equal(wl_zephyr_uart_dma_service(&fixture.adapter, 1U), WL_ERR_WOULD_BLOCK);
  zassert_true(fixture.adapter.slots[0].driver_owned);
  zassert_true(fixture.adapter.slots[1].driver_owned);
  release_all_and_disable();
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter, 2U));
}

ZTEST(wirelink_uart_dma_adapter, test_stopping_bytes_do_not_make_driver_wait_runnable) {
  init_fixture(&fixture);
  zassert_ok(wl_zephyr_uart_dma_stop(&fixture.adapter));
  /* RX disable may report a final prefix before returning its buffer. Stop
   * discards that prefix; room in the core ring is not executable stop work. */
  memset(fake_data.rx_buf, 0x5a, 4U);
  emit_rx_ready(0U, 4U);
  zassert_equal(wl_zephyr_uart_dma_service(&fixture.adapter, 1U), WL_ERR_WOULD_BLOCK);
  zassert_true(wl_rx_has_space(&fixture.link));
  wl_readiness_t ready = wl_zephyr_uart_dma_readiness(&fixture.adapter, &fixture.link, 1U);
  zassert_false(ready.runnable);
  zassert_true(ready.blocked);
  zassert_equal(ready.next_deadline_ms, WL_NO_DEADLINE);
  release_all_and_disable();
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter, 2U));
}

static void suspend_callback(void *unused) {
  ARG_UNUSED(unused);
  if (k_current_get() != &callback_thread) return;
  /* Test-only suspension models a callback preempted just before dispatch exit. */
  k_sem_give(&callback_entered);
  (void)k_sem_take(&callback_continue, K_FOREVER);
}

static void finish_disabled_callback(void *a, void *b, void *c) {
  ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
  emit_rx_disabled();
}

static void run_quiesce(void *a, void *b, void *c) {
  ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
  wl_zephyr_uart_dma_quiesce(&fixture.adapter);
  k_sem_give(&quiesce_done);
}

ZTEST(wirelink_uart_dma_adapter, test_quiesce_full_ring_waits_for_callback_exit) {
  init_fixture(&fixture);
  fill_ring_and_staging();
  zassert_ok(wl_zephyr_uart_dma_stop(&fixture.adapter));
  struct uart_event returned = {.type = UART_RX_BUF_RELEASED};
  returned.data.rx_buf.buf = fake_data.rx_buf;
  fake_data.callback(&fake_uart, &returned, fake_data.callback_data);
  if (fake_data.rx_next_buf != NULL) {
    returned.data.rx_buf.buf = fake_data.rx_next_buf;
    fake_data.callback(&fake_uart, &returned, fake_data.callback_data);
  }
  k_sem_reset(&callback_detached);
  k_sem_reset(&callback_entered);
  k_sem_reset(&callback_continue);
  k_sem_reset(&quiesce_done);
  fixture.adapter.config.notify = suspend_callback;
  k_thread_create(&callback_thread, callback_stack, K_THREAD_STACK_SIZEOF(callback_stack),
                  finish_disabled_callback, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
  zassert_ok(k_sem_take(&callback_entered, K_SECONDS(1)));
  k_thread_create(&quiesce_thread, quiesce_stack, K_THREAD_STACK_SIZEOF(quiesce_stack),
                  run_quiesce, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
  /* Full-ring staging must not prevent detachment, but the entered callback
   * must exit before quiesce returns and its adapter memory can be freed. */
  int detached = k_sem_take(&callback_detached, K_SECONDS(1));
  int finished_early = k_sem_take(&quiesce_done, K_NO_WAIT);
  k_sem_give(&callback_continue);
  zassert_ok(detached);
  zassert_not_equal(finished_early, 0);
  zassert_ok(k_sem_take(&quiesce_done, K_SECONDS(1)));
  zassert_ok(k_thread_join(&callback_thread, K_SECONDS(1)));
  zassert_ok(k_thread_join(&quiesce_thread, K_SECONDS(1)));
  zassert_true(fixture.adapter.detached);
  zassert_is_null(fake_data.callback);
  zassert_false(wl_rx_has_space(&fixture.link));
}

static void close_fixture(void *unused) {
  ARG_UNUSED(unused);
  if (fixture.adapter.detached) return;
  zassert_ok(wl_zephyr_uart_dma_stop(&fixture.adapter));
  if (fake_data.tx_active) emit_tx_event(UART_TX_ABORTED, 0U);
  if (fake_data.rx_enabled) release_all_and_disable();
  wl_zephyr_uart_dma_quiesce(&fixture.adapter);
}

ZTEST(wirelink_uart_dma_adapter, test_old_callback_cannot_access_reused_adapter) {
  init_fixture(&fixture);
  uart_callback_t old_callback = fake_data.callback;
  void *old_context = fake_data.callback_data;
  close_fixture(NULL);
  memset(&fixture.adapter, 0xa5, sizeof(fixture.adapter));
  struct uart_event late = {.type = UART_RX_DISABLED};
  old_callback(&fake_uart, &late, old_context);
  init_fixture(&fixture);
  uint8_t before[sizeof(fixture.adapter)];
  memcpy(before, &fixture.adapter, sizeof(before));
  old_callback(&fake_uart, &late, old_context);
  zassert_mem_equal(before, &fixture.adapter, sizeof(before));
  zassert_true(fake_data.rx_enabled);
}

ZTEST(wirelink_uart_dma_adapter, test_mismatched_completion_preserves_current_lease) {
  init_fixture(&fixture);
  zassert_ok(wl_send_unreliable(&fixture.link, 0x108U, NULL, 0U));
  const uint8_t other = 0U;
  struct uart_event late = {.type = UART_TX_DONE,
    .data.tx = {.buf = &other, .len = fake_data.tx_len}};
  fake_data.callback(&fake_uart, &late, fake_data.callback_data);
  zassert_equal(wl_ctx_impl(&fixture.link)->tx_inflight, 1U);
  zassert_equal(atomic_get(&fixture.adapter.tx_completion), 0);
  emit_tx_event(UART_TX_DONE, fake_data.tx_len);
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter, 1U));
  wl_event_t event;
  zassert_ok(wl_poll(&fixture.link, 1U, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
  zassert_equal(wl_poll(&fixture.link, 1U, &event), WL_ERR_NO_DATA);
}
static struct k_thread dma_wait_thread;
K_THREAD_STACK_DEFINE(dma_wait_stack, 2048);
static wl_err_t dma_wait_result;
static void run_dma_wait(void *a, void *b, void *c) {
  (void)a; (void)b; (void)c;
  dma_wait_result = wl_zephyr_uart_dma_wait(&fixture.adapter, K_FOREVER);
}
ZTEST(wirelink_uart_dma_adapter, test_stop_interrupts_public_wait_but_quiesce_still_joins) {
  init_fixture(&fixture);
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter, 0U));
  dma_wait_result = WL_ERR_INVALID_STATE;
  k_thread_create(&dma_wait_thread, dma_wait_stack, K_THREAD_STACK_SIZEOF(dma_wait_stack),
      run_dma_wait, NULL, NULL, NULL, K_PRIO_COOP(0), 0, K_NO_WAIT);
  zassert_equal(dma_wait_result, WL_ERR_INVALID_STATE);
  zassert_ok(wl_zephyr_uart_dma_stop(&fixture.adapter));
  zassert_ok(k_thread_join(&dma_wait_thread, K_SECONDS(1)));
  zassert_equal(dma_wait_result, WL_ERR_CANCELLED);
  zassert_equal(wl_zephyr_uart_dma_wait(&fixture.adapter, K_FOREVER), WL_ERR_CANCELLED);
  /* Cancellation of business waiting does not return the driver's buffers. */
  zassert_equal(wl_zephyr_uart_dma_service(&fixture.adapter, 0U), WL_ERR_WOULD_BLOCK);
  release_all_and_disable();
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter, 0U));
  zassert_equal(wl_zephyr_uart_dma_wait(&fixture.adapter, K_NO_WAIT), WL_ERR_CANCELLED);
}
ZTEST_SUITE(wirelink_uart_dma_adapter, NULL, NULL, NULL, close_fixture, NULL);

/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
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
  const uint8_t *tx_buf;
  size_t tx_len;
  int next_tx_result;
  bool rx_enabled;
  bool tx_active;
  bool tx_idle;
  size_t callback_sets;
  size_t rx_enables;
  size_t rx_disables;
  size_t tx_calls;
  size_t tx_aborts;
};

static struct fake_uart_data fake_data;
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
  ARG_UNUSED(dev);
  ARG_UNUSED(buf);
  ARG_UNUSED(len);
  return -EBUSY;
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
};

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
      .maximum_chunk = fixture->storage.rx_fifo_size,
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

static void emit_rx_released(void) {
  struct uart_event event = {
      .type = UART_RX_BUF_RELEASED,
      .data.rx_buf = {.buf = fake_data.rx_buf},
  };

  fake_data.callback(&fake_uart, &event, fake_data.callback_data);
}

static void emit_rx_disabled(void) {
  struct uart_event event = {.type = UART_RX_DISABLED};

  fake_data.rx_enabled = false;
  fake_data.rx_buf = NULL;
  fake_data.rx_len = 0U;
  fake_data.callback(&fake_uart, &event, fake_data.callback_data);
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
  struct fixture fixture;
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

  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter));
  zassert_equal(wl_ctx_impl(&fixture.link)->tx_inflight, 0U);
  zassert_ok(wl_poll(&fixture.link, 1U, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
}

ZTEST(wirelink_uart_dma_adapter, test_tx_done_waits_for_physical_idle) {
  struct fixture fixture;
  const uint8_t payload[] = {0x31U, 0x32U};
  wl_event_t event = {0};

  init_fixture(&fixture);
  zassert_ok(
      wl_send_unreliable(&fixture.link, 0x108U, payload, sizeof(payload)));
  emit_tx_done_before_idle();
  zassert_equal(wl_zephyr_uart_dma_service(&fixture.adapter),
                WL_ERR_WOULD_BLOCK);
  zassert_equal(wl_ctx_impl(&fixture.link)->tx_inflight, 1U);

  fake_data.tx_idle = true;
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter));
  zassert_equal(wl_ctx_impl(&fixture.link)->tx_inflight, 0U);
  zassert_ok(wl_poll(&fixture.link, 1U, &event));
  zassert_equal(event.type, WL_EVT_TX_SUCCESS);
}

ZTEST(wirelink_uart_dma_adapter, test_tx_busy_retries_and_failure_maps_to_io) {
  struct fixture fixture;
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
  zassert_true(fake_data.tx_active);
  emit_tx_event(UART_TX_DONE, fake_data.tx_len);
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter));
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
  struct fixture fixture;
  const uint8_t payload[] = {0x66U, 0x77U};
  wl_tx_handle_t handle = 0U;
  wl_tx_state_t state = WL_TX_STATE_IDLE;

  init_fixture(&fixture);
  zassert_ok(wl_send_reliable(&fixture.link, 0x104U, payload, sizeof(payload),
                              &handle));
  zassert_not_equal(handle, 0U);
  emit_tx_event(UART_TX_ABORTED, 1U);
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter));
  zassert_equal(fake_data.tx_calls, 2U,
                "completion service must permit synchronous retry submit");
  zassert_true(fake_data.tx_active);

  emit_tx_event(UART_TX_DONE, fake_data.tx_len);
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter));
  zassert_ok(wl_tx_status(&fixture.link, handle, &state));
  zassert_equal(state, WL_TX_STATE_WAITING_ACK);
}

ZTEST(wirelink_uart_dma_adapter, test_rx_release_restarts_and_decodes) {
  struct fixture fixture;
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
  emit_rx_released();
  emit_rx_disabled();

  zassert_ok(wl_poll(&fixture.link, 1U, &event));
  zassert_equal(event.type, WL_EVT_UNRELIABLE_RX);
  zassert_mem_equal(event.payload, payload, sizeof(payload));
  wl_event_release(&fixture.link, &event);
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter));
  zassert_true(fake_data.rx_enabled);
  zassert_equal(fake_data.rx_enables, 2U);
}

ZTEST(wirelink_uart_dma_adapter, test_stop_waits_for_driver_and_can_restart) {
  struct fixture fixture;
  const uint8_t payload[] = {0x88U, 0x99U};
  wl_zephyr_uart_dma_stats_t stats = {0};

  init_fixture(&fixture);
  zassert_ok(
      wl_send_unreliable(&fixture.link, 0x105U, payload, sizeof(payload)));
  zassert_ok(wl_zephyr_uart_dma_stop(&fixture.adapter));
  zassert_equal(fake_data.rx_disables, 1U);
  zassert_equal(fake_data.tx_aborts, 1U);
  zassert_equal(wl_zephyr_uart_dma_service(&fixture.adapter),
                WL_ERR_WOULD_BLOCK);

  emit_tx_event(UART_TX_ABORTED, 0U);
  emit_rx_released();
  emit_rx_disabled();
  zassert_ok(wl_zephyr_uart_dma_service(&fixture.adapter));
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

ZTEST_SUITE(wirelink_uart_dma_adapter, NULL, NULL, NULL, NULL, NULL);

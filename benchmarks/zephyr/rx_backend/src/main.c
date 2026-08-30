/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <esp_cpu.h>

#if defined(WL_BENCH_INGRESS_DMA)
#include "wirelink/zephyr/uart_dma.h"
#endif

#if defined(WL_BENCH_INGRESS_USB)
#include <sample_usbd.h>
#include <zephyr/usb/usbd.h>
#endif

#include "wirelink/frame.h"
#include "wirelink/wirelink.h"

#define BENCH_RX_USABLE 4096U
#define BENCH_FALLBACK_SIZE WL_FRAME_MAX_COBS_LEN
#define BENCH_UART_CHUNK 64U
#ifndef BENCH_PRIMITIVE_WARMUP
#define BENCH_PRIMITIVE_WARMUP 10000U
#endif
#ifndef BENCH_PRIMITIVE_SAMPLES
#define BENCH_PRIMITIVE_SAMPLES 100000U
#endif
#ifndef BENCH_WARMUP_FRAMES
#define BENCH_WARMUP_FRAMES 1000U
#endif
#ifndef BENCH_SAMPLE_FRAMES
#define BENCH_SAMPLE_FRAMES 10000U
#endif
#ifndef BENCH_FRAME_TIMEOUT_MS
#define BENCH_FRAME_TIMEOUT_MS 1000U
#endif
#ifndef BENCH_UART_TIMEOUT_US
#define BENCH_UART_TIMEOUT_US 200
#endif
#ifndef BENCH_DMA_MAX_CHUNK
#define BENCH_DMA_MAX_CHUNK BENCH_RX_USABLE
#endif
#ifndef BENCH_PAYLOAD_START_INDEX
#define BENCH_PAYLOAD_START_INDEX 0U
#endif
#ifndef BENCH_PAYLOAD_END_INDEX
#define BENCH_PAYLOAD_END_INDEX UINT32_MAX
#endif

static wl_ctx_t link_ctx;
static wl_config_t link_config;
static wl_storage_t link_storage;
static uint8_t tx_payload[WL_FRAME_MAX_PAYLOAD];
static uint8_t tx_unit[WL_FRAME_MAX_COBS_LEN];
static uint8_t control_unit[128U];
static uint8_t rx_fifo[BENCH_RX_USABLE] __aligned(64);
static uint8_t rx_fallback[BENCH_FALLBACK_SIZE] __aligned(64);
static uint8_t primitive_bytes[256U];

#if !defined(WL_BENCH_INGRESS_USB)
static const size_t payload_sizes[] = {16U, 20U, 64U, 120U, 256U, 1024U,
                                       2048U};
static uint8_t frame_payload[WL_FRAME_MAX_PAYLOAD];
static uint8_t frame_wire[WL_FRAME_MAX_COBS_LEN];
static uint64_t latency_samples[BENCH_SAMPLE_FRAMES];
#endif

static atomic_t producer_calls;
static atomic_t producer_cycles;
static atomic_t accepted_bytes;
static atomic_t dropped_bytes;
static atomic_t ingress_errors;

#if defined(WL_BENCH_INGRESS_DMA)
static wl_zephyr_uart_dma_t dma_adapter;
#endif

static uint32_t bench_cycle_count(void) {
  return (uint32_t)esp_cpu_get_cycle_count();
}

#if defined(WL_BENCH_INGRESS_DMA)
static uint32_t dma_cycle_counter(void *user_data) {
  ARG_UNUSED(user_data);
  return bench_cycle_count();
}
#endif

static wl_sink_result_t discard_sink(void *user_data, wl_io_token_t token,
                                     const uint8_t *data, size_t len) {
  ARG_UNUSED(user_data);
  ARG_UNUSED(token);
  ARG_UNUSED(data);
  ARG_UNUSED(len);
  return WL_SINK_SENT;
}

static const char *backend_name(void) {
  return "bipbuf_spsc";
}

static const char *ingress_name(void) {
#if defined(WL_BENCH_INGRESS_DMA)
  return "uart_dma";
#elif defined(WL_BENCH_INGRESS_USB)
  return "usb_cdc_dma";
#else
  return "uart_irq";
#endif
}

static size_t physical_ring_size(void) {
  return BENCH_RX_USABLE;
}

static int init_link(void) {
  wl_storage_requirements_t requirements;

  link_config = (wl_config_t){
      .max_payload_len = WL_FRAME_MAX_PAYLOAD,
      .envelope = WL_ENVELOPE_COBS_STREAM,
      .integrity = WL_INTEGRITY_CRC32C,
      .session_id = UINT64_C(0x574C42584D41524B),
      .max_retries = 1U,
      .ack_timeout_ms = 100U,
      .max_transmission_unit = sizeof(tx_unit),
  };
  if (wl_config_requirements(&link_config, &requirements) != WL_OK) {
    return -EINVAL;
  }
  if (requirements.rx_fifo_size > sizeof(rx_fifo) ||
      requirements.rx_fallback_size > sizeof(rx_fallback)) {
    return -ENOMEM;
  }

  link_storage = (wl_storage_t){
      .tx_payload = tx_payload,
      .tx_payload_size = sizeof(tx_payload),
      .tx_unit = tx_unit,
      .tx_unit_size = sizeof(tx_unit),
      .control_unit = control_unit,
      .control_unit_size = sizeof(control_unit),
      .rx_fifo = rx_fifo,
      .rx_fifo_size = sizeof(rx_fifo),
      .rx_fallback = rx_fallback,
      .rx_fallback_size = sizeof(rx_fallback),
  };

  if (wl_init(&link_ctx, &link_config, &link_storage) != WL_OK) {
    return -EINVAL;
  }
  return wl_set_sink(&link_ctx, discard_sink, NULL) == WL_OK ? 0 : -EINVAL;
}

static int consume_primitive_unit(void) {
  wl_event_t event = {0};
  int ret = wl_poll(&link_ctx, (wl_time_ms_t)k_uptime_get_32(), &event);

  if (ret == WL_OK) {
    wl_event_release(&link_ctx, &event);
    return -EBADMSG;
  }
  return ret == WL_ERR_NO_DATA ? 0 : -EIO;
}

static int run_reserve_commit_primitive(size_t chunk_length) {
  const size_t total = BENCH_PRIMITIVE_WARMUP + BENCH_PRIMITIVE_SAMPLES;
  uint64_t measured_cycles = 0U;
  size_t measured_calls = 0U;

  for (size_t operation = 0U; operation < total; ++operation) {
    size_t offset = 0U;

    while (offset < chunk_length) {
      wl_span_t span = {0};
      uint32_t started = bench_cycle_count();
      int ret = wl_rx_reserve(&link_ctx, &span);
      uint32_t reserve_cycles = bench_cycle_count() - started;
      size_t part;

      if (ret != WL_OK || span.length == 0U) {
        return -EIO;
      }
      part = MIN(span.length, chunk_length - offset);
      memset(span.data, 0xA5, part);
      if (offset + part == chunk_length) {
        span.data[part - 1U] = 0U;
      }
      started = bench_cycle_count();
      ret = wl_rx_commit(&link_ctx, part);
      if (operation >= BENCH_PRIMITIVE_WARMUP) {
        measured_cycles += reserve_cycles + bench_cycle_count() - started;
        ++measured_calls;
      }
      if (ret != WL_OK) {
        return -EIO;
      }
      offset += part;
    }
    if (consume_primitive_unit() != 0) {
      return -EIO;
    }
  }

  printk("wirelink_rx_primitive_v1,%s,reserve_commit,%u,%u,%u,%llu\n",
         backend_name(), (unsigned int)chunk_length,
         BENCH_PRIMITIVE_SAMPLES, (unsigned int)measured_calls,
         (unsigned long long)measured_cycles);
  return 0;
}

static int run_feed_primitive(size_t chunk_length) {
  const size_t total = BENCH_PRIMITIVE_WARMUP + BENCH_PRIMITIVE_SAMPLES;
  uint64_t measured_cycles = 0U;

  memset(primitive_bytes, 0x5A, chunk_length);
  primitive_bytes[chunk_length - 1U] = 0U;
  for (size_t operation = 0U; operation < total; ++operation) {
    size_t accepted = 0U;
    uint32_t started = bench_cycle_count();
    int ret = wl_feed_bytes(&link_ctx, primitive_bytes, chunk_length,
                            &accepted);
    uint32_t elapsed = bench_cycle_count() - started;

    if (ret != WL_OK || accepted != chunk_length) {
      return -EIO;
    }
    if (operation >= BENCH_PRIMITIVE_WARMUP) {
      measured_cycles += elapsed;
    }
    if (consume_primitive_unit() != 0) {
      return -EIO;
    }
  }

  printk("wirelink_rx_primitive_v1,%s,feed,%u,%u,%u,%llu\n",
         backend_name(), (unsigned int)chunk_length,
         BENCH_PRIMITIVE_SAMPLES, BENCH_PRIMITIVE_SAMPLES,
         (unsigned long long)measured_cycles);
  return 0;
}

static int run_primitive_profiles(void) {
  static const size_t chunk_lengths[] = {1U, 8U, 32U, 64U, 256U};

  printk("wirelink_rx_primitive_v1,backend,operation,chunk,operations,"
         "producer_calls,producer_cycles\n");
  for (size_t i = 0U; i < ARRAY_SIZE(chunk_lengths); ++i) {
    int ret = run_reserve_commit_primitive(chunk_lengths[i]);

    if (ret == 0) {
      ret = run_feed_primitive(chunk_lengths[i]);
    }
    if (ret != 0) {
      return ret;
    }
  }
  return 0;
}

#if !defined(WL_BENCH_INGRESS_USB)
static void reset_ingress_stats(void) {
  atomic_set(&producer_calls, 0);
  atomic_set(&producer_cycles, 0);
  atomic_set(&accepted_bytes, 0);
  atomic_set(&dropped_bytes, 0);
  atomic_set(&ingress_errors, 0);
#if defined(WL_BENCH_INGRESS_DMA)
  wl_zephyr_uart_dma_reset_stats(&dma_adapter);
#endif
}
#endif

#if !defined(WL_BENCH_INGRESS_DMA)
static void account_feed(const uint8_t *data, size_t len) {
  uint32_t started = bench_cycle_count();
  size_t accepted = 0U;
  int ret = wl_feed_bytes(&link_ctx, data, len, &accepted);
  uint32_t elapsed = bench_cycle_count() - started;

  atomic_inc(&producer_calls);
  atomic_add(&producer_cycles, (atomic_val_t)elapsed);
  atomic_add(&accepted_bytes, (atomic_val_t)accepted);
  atomic_add(&dropped_bytes, (atomic_val_t)(len - accepted));
  if (ret != WL_OK && ret != WL_ERR_WOULD_BLOCK) {
    atomic_inc(&ingress_errors);
  }
}
#endif

#if !defined(WL_BENCH_INGRESS_USB)
static const struct device *const rx_uart = DEVICE_DT_GET(DT_NODELABEL(uart1));
static const struct device *const tx_uart = DEVICE_DT_GET(DT_NODELABEL(uart0));

#if defined(WL_BENCH_INGRESS_DMA)
static int validate_uart_dma_tx(void) {
  static const uint8_t payload[] = {0x57U, 0x4CU, 0x54U, 0x58U};
  int64_t deadline = k_uptime_get() + BENCH_FRAME_TIMEOUT_MS;
  int ret = wl_send_unreliable(&link_ctx, 0x7FFEU, payload, sizeof(payload));

  if (ret != WL_OK) {
    return ret;
  }
  while (k_uptime_get() < deadline) {
    wl_event_t event = {0};

    ret = wl_zephyr_uart_dma_service(&dma_adapter);
    if (ret != WL_OK && ret != WL_ERR_WOULD_BLOCK) {
      return ret;
    }
    ret = wl_poll(&link_ctx, (wl_time_ms_t)k_uptime_get_32(), &event);
    if (ret == WL_OK && event.type == WL_EVT_TX_SUCCESS) {
      return 0;
    }
    if (ret == WL_OK) {
      wl_event_release(&link_ctx, &event);
    } else if (ret != WL_ERR_NO_DATA) {
      return ret;
    }
    k_yield();
  }
  return -ETIMEDOUT;
}
#endif

static int uart_tx_frame(const uint8_t *data, size_t length) {
  int64_t deadline = k_uptime_get() + BENCH_FRAME_TIMEOUT_MS;
  size_t offset = 0U;

  if (data == NULL || length == 0U) {
    return -EINVAL;
  }
  /* ESP32-S3 UART async instances share UHCI0. Leave it to RX DMA and feed
   * the independent source UART in bounded FIFO chunks without TX IRQs. */
  while (offset < length && k_uptime_get() < deadline) {
    size_t chunk = MIN(length - offset, BENCH_UART_CHUNK);
    int sent = uart_fifo_fill(tx_uart, data + offset, chunk);

    if (sent < 0) {
      return -EIO;
    }
    if (sent == 0) {
      k_yield();
      continue;
    }
    offset += (size_t)sent;
  }
  while (offset == length && uart_irq_tx_complete(tx_uart) == 0 &&
         k_uptime_get() < deadline) {
    k_yield();
  }
  if (offset != length || uart_irq_tx_complete(tx_uart) == 0) {
    return -ETIMEDOUT;
  }
  return 0;
}

#if defined(WL_BENCH_INGRESS_IRQ)
static void uart_irq_ingress(const struct device *dev, void *user_data) {
  uint8_t chunk[BENCH_UART_CHUNK];

  ARG_UNUSED(user_data);
  uart_irq_update(dev);
  while (uart_irq_is_pending(dev) > 0) {
    if (uart_irq_rx_ready(dev) > 0) {
      int received = uart_fifo_read(dev, chunk, sizeof(chunk));
      if (received > 0) {
        account_feed(chunk, (size_t)received);
      } else if (received < 0) {
        atomic_inc(&ingress_errors);
      }
    }
    uart_irq_update(dev);
  }
}
#endif

static int init_uart_ingress(void) {
  struct uart_config config = {
      .baudrate = 3000000U,
      .parity = UART_CFG_PARITY_NONE,
      .stop_bits = UART_CFG_STOP_BITS_1,
      .data_bits = UART_CFG_DATA_BITS_8,
      .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
  };

  if (!device_is_ready(rx_uart) || !device_is_ready(tx_uart) ||
      uart_configure(rx_uart, &config) != 0 ||
      uart_configure(tx_uart, &config) != 0) {
    return -ENODEV;
  }
#if defined(WL_BENCH_INGRESS_DMA)
  wl_zephyr_uart_dma_config_t dma_config = {
      .uart = rx_uart,
      .link = &link_ctx,
      .maximum_chunk = BENCH_DMA_MAX_CHUNK,
      .timeout_us = BENCH_UART_TIMEOUT_US,
      .tx_timeout_us = SYS_FOREVER_US,
      .wait_for_tx_idle = true,
      .cycle_counter = dma_cycle_counter,
  };

  if (wl_zephyr_uart_dma_init(&dma_adapter, &dma_config) != WL_OK ||
      wl_zephyr_uart_dma_start(&dma_adapter) != WL_OK) {
    return -ENOTSUP;
  }
  return 0;
#else
  if (uart_irq_callback_user_data_set(rx_uart, uart_irq_ingress, NULL) != 0) {
    return -ENOTSUP;
  }
  uart_irq_rx_enable(rx_uart);
  return 0;
#endif
}

static size_t encode_frame(size_t payload_len, uint32_t sequence) {
  wl_wire_packet_t packet = {
      .type = WL_PACKET_DATA,
      .integrity = WL_INTEGRITY_CRC32C,
      .flags = 0U,
      .cmd_id = 1U,
      .session_id = UINT64_C(0x1122334455667788),
      .sequence = sequence,
      .payload = frame_payload,
      .payload_len = payload_len,
  };
  size_t wire_len = 0U;

  for (size_t i = 0U; i < payload_len; ++i) {
    frame_payload[i] = (uint8_t)((i * 33U + sequence * 17U) & 0xFFU);
  }
  if (wl_frame_encode(&packet, WL_ENVELOPE_COBS_STREAM, frame_wire,
                      sizeof(frame_wire), &wire_len) != WL_OK) {
    return 0U;
  }
  return wire_len;
}

#if defined(WL_BENCH_INGRESS_DMA)
static int ensure_dma_running(void) {
  int64_t deadline = k_uptime_get() + BENCH_FRAME_TIMEOUT_MS;

  while (k_uptime_get() < deadline) {
    wl_zephyr_uart_dma_stats_t stats = {0};
    int ret = wl_zephyr_uart_dma_service(&dma_adapter);

    if (ret != WL_OK && ret != WL_ERR_WOULD_BLOCK && ret != WL_ERR_BUSY) {
      return -EIO;
    }
    wl_zephyr_uart_dma_get_stats(&dma_adapter, &stats);
    if (stats.running != 0U) {
      if (BENCH_UART_TIMEOUT_US == SYS_FOREVER_US) {
        return 0;
      }
      for (size_t i = 0U; i < WL_RX_DMA_MAX_CLAIMS; ++i) {
        if (dma_adapter.slots[i].claim.token != 0U &&
            dma_adapter.slots[i].received == 0U &&
            dma_adapter.slots[i].published == 0U &&
            !atomic_test_bit(&dma_adapter.released_slots, (int)i)) {
          return 0;
        }
      }
    }
    k_busy_wait(10U);
  }
  return -ETIMEDOUT;
}
#endif

static int wait_for_frame(size_t payload_len, uint32_t started,
                          uint64_t *out_latency) {
  int64_t deadline = k_uptime_get() + BENCH_FRAME_TIMEOUT_MS;

  while (k_uptime_get() < deadline) {
    wl_event_t event = {0};
    int ret = wl_poll(&link_ctx, (wl_time_ms_t)k_uptime_get_32(), &event);

    if (ret == WL_OK && (event.type == WL_EVT_UNRELIABLE_RX ||
                         event.type == WL_EVT_RELIABLE_RX)) {
      bool valid = event.payload_len == payload_len &&
                   memcmp(event.payload, frame_payload, payload_len) == 0;
      *out_latency = (uint32_t)(bench_cycle_count() - started);
      wl_event_release(&link_ctx, &event);
#if defined(WL_BENCH_INGRESS_DMA)
      (void)wl_zephyr_uart_dma_service(&dma_adapter);
#endif
      return valid ? 0 : -EBADMSG;
    }
    if (ret != WL_OK && ret != WL_ERR_NO_DATA) {
      return -EIO;
    }
#if defined(WL_BENCH_INGRESS_DMA)
    (void)wl_zephyr_uart_dma_service(&dma_adapter);
#endif
    k_yield();
  }
#if defined(WL_BENCH_INGRESS_DMA)
  {
    wl_rx_counters_t counters = {0};
    wl_zephyr_uart_dma_stats_t stats = {0};

    (void)wl_rx_get_counters(&link_ctx, &counters);
    wl_zephyr_uart_dma_get_stats(&dma_adapter, &stats);
    printk("wirelink_rx_dma_timeout_v1,%u,%u,%u,%u,%u,%u,%u,%u,"
           "%u,%u,%u,%u,%u,%u,%u,%u\n",
           (unsigned int)payload_len, counters.malformed,
           counters.bad_integrity, counters.overflow, stats.errors,
           stats.running, stats.paused, stats.published_bytes,
           dma_adapter.slots[0].claim.token != 0U ? 1U : 0U,
           (unsigned int)dma_adapter.slots[0].received,
           (unsigned int)dma_adapter.slots[0].published,
           dma_adapter.slots[0].claim.token != 0U &&
                   !atomic_test_bit(&dma_adapter.released_slots, 0)
               ? 1U
               : 0U,
           dma_adapter.slots[1].claim.token != 0U ? 1U : 0U,
           (unsigned int)dma_adapter.slots[1].received,
           (unsigned int)dma_adapter.slots[1].published,
           dma_adapter.slots[1].claim.token != 0U &&
                   !atomic_test_bit(&dma_adapter.released_slots, 1)
               ? 1U
               : 0U);
  }
#endif
  return -ETIMEDOUT;
}

static void sort_latencies(size_t count) {
  for (size_t gap = count / 2U; gap != 0U; gap /= 2U) {
    for (size_t i = gap; i < count; ++i) {
      uint64_t value = latency_samples[i];
      size_t j = i;
      while (j >= gap && latency_samples[j - gap] > value) {
        latency_samples[j] = latency_samples[j - gap];
        j -= gap;
      }
      latency_samples[j] = value;
    }
  }
}

static int run_uart_profile(size_t payload_len) {
  const size_t total = BENCH_WARMUP_FRAMES + BENCH_SAMPLE_FRAMES;
  uint64_t run_cycles = 0U;
#if defined(WL_BENCH_INGRESS_DMA)
  wl_zephyr_uart_dma_stats_t dma_stats;
#endif

  reset_ingress_stats();
  for (size_t i = 0U; i < total; ++i) {
    size_t wire_len = encode_frame(payload_len, (uint32_t)i);
    uint32_t started;
    uint64_t latency;

    if (wire_len == 0U) {
      return -EINVAL;
    }
#if defined(WL_BENCH_INGRESS_DMA)
    {
      int ret = ensure_dma_running();

      if (ret != 0) {
        return ret;
      }
    }
#endif
    started = bench_cycle_count();
    int ret = uart_tx_frame(frame_wire, wire_len);

    if (ret != 0) {
      return ret;
    }
    ret = wait_for_frame(payload_len, started, &latency);

    if (ret != 0) {
      return ret;
    }
    if (i >= BENCH_WARMUP_FRAMES) {
      latency_samples[i - BENCH_WARMUP_FRAMES] = latency;
      run_cycles += latency;
    } else if (i + 1U == BENCH_WARMUP_FRAMES) {
      reset_ingress_stats();
    }
  }

  sort_latencies(BENCH_SAMPLE_FRAMES);
#if defined(WL_BENCH_INGRESS_DMA)
  wl_zephyr_uart_dma_get_stats(&dma_adapter, &dma_stats);
#endif
  printk(
      "%s,%s,%s,%u,%u,%llu,%lld,%lld,%lld,%lld,%llu,%llu,%llu,%llu\n",
      "wirelink_rx_bench_v1", backend_name(), ingress_name(),
      (unsigned int)payload_len, BENCH_SAMPLE_FRAMES,
      (unsigned long long)run_cycles,
      (long long)
#if defined(WL_BENCH_INGRESS_DMA)
          dma_stats.rx_ready_events,
      (long long)dma_stats.producer_cycles,
      (long long)dma_stats.published_bytes,
#else
          atomic_get(&producer_calls),
      (long long)atomic_get(&producer_cycles),
      (long long)atomic_get(&accepted_bytes),
#endif
      (long long)atomic_get(&dropped_bytes),
      (unsigned long long)latency_samples[BENCH_SAMPLE_FRAMES / 2U],
      (unsigned long long)latency_samples[(BENCH_SAMPLE_FRAMES * 95U) / 100U],
      (unsigned long long)latency_samples[(BENCH_SAMPLE_FRAMES * 99U) / 100U],
      (unsigned long long)latency_samples[BENCH_SAMPLE_FRAMES - 1U]);
  return atomic_get(&ingress_errors) == 0
#if defined(WL_BENCH_INGRESS_DMA)
                 && dma_stats.errors == 0U
#endif
             ? 0
             : -EIO;
}
#endif /* !WL_BENCH_INGRESS_USB */

#if defined(WL_BENCH_INGRESS_USB)
static const struct device *const cdc_uart =
    DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));
static struct usbd_context *usb_context;

static void usb_message(struct usbd_context *const context,
                        const struct usbd_msg *message) {
  if (usbd_can_detect_vbus(context) && message->type == USBD_MSG_VBUS_READY) {
    (void)usbd_enable(context);
  }
  if (usbd_can_detect_vbus(context) && message->type == USBD_MSG_VBUS_REMOVED) {
    (void)usbd_disable(context);
  }
}

static void usb_cdc_ingress(const struct device *dev, void *user_data) {
  uint8_t chunk[BENCH_UART_CHUNK];

  ARG_UNUSED(user_data);
  uart_irq_update(dev);
  while (uart_irq_is_pending(dev) > 0) {
    if (uart_irq_rx_ready(dev) > 0) {
      int received = uart_fifo_read(dev, chunk, sizeof(chunk));
      if (received > 0) {
        account_feed(chunk, (size_t)received);
      }
    }
    uart_irq_update(dev);
  }
}

static int init_usb_ingress(void) {
  int ret;

  if (!device_is_ready(cdc_uart)) {
    return -ENODEV;
  }
  usb_context = sample_usbd_init_device(usb_message);
  if (usb_context == NULL) {
    return -ENODEV;
  }
  if (!usbd_can_detect_vbus(usb_context)) {
    ret = usbd_enable(usb_context);
    if (ret != 0) {
      return ret;
    }
  }
  ret = uart_irq_callback_user_data_set(cdc_uart, usb_cdc_ingress, NULL);
  if (ret == 0) {
    uart_irq_rx_enable(cdc_uart);
  }
  return ret;
}

static void run_usb_ingress(void) {
  uint32_t frames = 0U;

  printk("wirelink_rx_bench_v1,%s,%s,ready\n", backend_name(), ingress_name());
  while (true) {
    wl_event_t event = {0};
    int ret = wl_poll(&link_ctx, (wl_time_ms_t)k_uptime_get_32(), &event);

    if (ret == WL_OK && (event.type == WL_EVT_UNRELIABLE_RX ||
                         event.type == WL_EVT_RELIABLE_RX)) {
      ++frames;
      if ((frames % BENCH_SAMPLE_FRAMES) == 0U) {
        wl_rx_counters_t counters = {0};
        (void)wl_rx_get_counters(&link_ctx, &counters);
        printk("wirelink_rx_bench_v1,%s,%s,%u,%u,%lld,%lld,%lld,%u\n",
               backend_name(), ingress_name(),
               (unsigned int)event.payload_len, frames,
               (long long)atomic_get(&producer_calls),
               (long long)atomic_get(&producer_cycles),
               (long long)atomic_get(&dropped_bytes), counters.overflow);
      }
      wl_event_release(&link_ctx, &event);
    } else {
      k_yield();
    }
  }
}
#endif

int main(void) {
  int ret = init_link();

  if (ret != 0) {
    printk("wirelink_rx_bench_v1,error,init_link,%d\n", ret);
    return 0;
  }
  ret = run_primitive_profiles();
  if (ret == 0) {
    ret = init_link();
  }
  if (ret != 0) {
    printk("wirelink_rx_bench_v1,error,primitive,%d\n", ret);
    return 0;
  }
  printk("wirelink_rx_bench_v1,meta,%s,%s,%u,%u,%u,%u\n",
         backend_name(), ingress_name(), sys_clock_hw_cycles_per_sec(),
         BENCH_RX_USABLE, (unsigned int)physical_ring_size(),
         BENCH_FALLBACK_SIZE);
  printk("wirelink_rx_bench_v1,backend,ingress,payload,frames,run_cycles,"
         "producer_calls,producer_cycles,accepted,dropped,latency_median,"
         "latency_p95,latency_p99,latency_max\n");

#if defined(WL_BENCH_INGRESS_USB)
  ret = init_usb_ingress();
  if (ret == 0) {
    run_usb_ingress();
  }
#else
  ret = init_uart_ingress();
#if defined(WL_BENCH_INGRESS_DMA)
  if (ret == 0) {
    ret = validate_uart_dma_tx();
    printk("wirelink_uart_dma_tx_v1,%s\n", ret == 0 ? "pass" : "fail");
  }
#endif
  if (ret == 0) {
    for (size_t i = BENCH_PAYLOAD_START_INDEX;
         i < ARRAY_SIZE(payload_sizes) && i < BENCH_PAYLOAD_END_INDEX; ++i) {
      ret = run_uart_profile(payload_sizes[i]);
      if (ret != 0) {
        break;
      }
    }
  }
#endif

#if defined(WL_BENCH_INGRESS_DMA)
  {
    wl_zephyr_uart_dma_stats_t dma_stats;

    wl_zephyr_uart_dma_get_stats(&dma_adapter, &dma_stats);
    printk("wirelink_rx_dma_stats_v1,%u,%u,%u,%u,%u,%u\n",
           dma_stats.buffer_requests, dma_stats.rx_ready_events,
           dma_stats.published_bytes, dma_stats.producer_cycles,
           dma_stats.errors, dma_stats.running);
  }
#endif
  printk("wirelink_rx_bench_v1,error,%s,%s,%d\n", backend_name(),
         ingress_name(), ret);
  return 0;
}

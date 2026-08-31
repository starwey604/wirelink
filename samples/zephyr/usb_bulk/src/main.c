/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <sample_usbd.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/usbd.h>

#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
#include <esp_cpu.h>
#endif

#include "wirelink/wirelink.h"
#include "wirelink/zephyr/usb_bulk.h"

#define MAX_PAYLOAD 512U
#define UNIT_STORAGE 576U
#define RX_RING_STORAGE 2048U
#define CPU_STATS_SNAPSHOT_PERIOD_MS 100

static wl_ctx_t link;
static wl_zephyr_usb_bulk_t usb_adapter;
static uint8_t tx_payload[MAX_PAYLOAD];
static uint8_t tx_unit[UNIT_STORAGE];
static uint8_t control_unit[64];
static uint8_t rx_fifo[RX_RING_STORAGE];
static uint8_t rx_fallback[UNIT_STORAGE];

#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
typedef struct sample_cpu_region_stats {
  uint64_t calls;
  uint64_t total_cycles;
  uint32_t max_cycles;
} sample_cpu_region_stats_t;

typedef struct sample_u32_accumulator {
  uint64_t value;
  uint32_t previous;
} sample_u32_accumulator_t;

typedef struct sample_cpu_region_accumulator {
  sample_u32_accumulator_t calls;
  sample_u32_accumulator_t total_cycles;
  uint32_t max_cycles;
} sample_cpu_region_accumulator_t;

typedef struct sample_adapter_accumulator {
  sample_u32_accumulator_t rx_claims;
  sample_u32_accumulator_t rx_completions;
  sample_u32_accumulator_t rx_bytes;
  sample_u32_accumulator_t rx_pauses;
  sample_u32_accumulator_t tx_submissions;
  sample_u32_accumulator_t tx_completions;
  sample_u32_accumulator_t rx_callback_cycles;
  uint32_t rx_callback_max_cycles;
  sample_u32_accumulator_t tx_callback_cycles;
  uint32_t tx_callback_max_cycles;
  sample_u32_accumulator_t tx_sink_cycles;
  uint32_t tx_sink_max_cycles;
  sample_cpu_region_accumulator_t active_service;
  sample_u32_accumulator_t errors;
} sample_adapter_accumulator_t;

static atomic_t dwc2_isr_calls;
static atomic_t dwc2_isr_cycles;
static atomic_t dwc2_isr_max_cycles;
static atomic_t dwc2_thread_calls;
static atomic_t dwc2_thread_cycles;
static atomic_t dwc2_thread_max_cycles;
static atomic_t usbd_thread_calls;
static atomic_t usbd_thread_cycles;
static atomic_t usbd_thread_max_cycles;
static sample_cpu_region_accumulator_t dwc2_isr_stats;
static sample_cpu_region_accumulator_t dwc2_thread_stats;
static sample_cpu_region_accumulator_t usbd_thread_stats;
static sample_adapter_accumulator_t adapter_stats;
static sample_cpu_region_stats_t wl_poll_stats;
static sample_cpu_region_stats_t rx_event_copy_release_stats;
static sample_cpu_region_stats_t wl_send_unreliable_stats;
static sample_cpu_region_stats_t usb_service_stats;

static void update_max(atomic_t *maximum, uint32_t value) {
  atomic_val_t previous = atomic_get(maximum);

  while (value > (uint32_t)previous &&
         !atomic_cas(maximum, previous, (atomic_val_t)value)) {
    previous = atomic_get(maximum);
  }
}

static void record_cpu_cycles(atomic_t *calls, atomic_t *total,
                              atomic_t *maximum, uint32_t cycles) {
  atomic_inc(calls);
  atomic_add(total, (atomic_val_t)cycles);
  update_max(maximum, cycles);
}

static void record_sample_cpu_cycles(sample_cpu_region_stats_t *stats,
                                     uint32_t cycles) {
  stats->calls++;
  stats->total_cycles += cycles;
  if (cycles > stats->max_cycles) {
    stats->max_cycles = cycles;
  }
}

uint32_t wirelink_usb_cpu_cycle_count(void) {
  return (uint32_t)esp_cpu_get_cycle_count();
}

void wirelink_usb_cpu_record_dwc2_isr(uint32_t cycles) {
  record_cpu_cycles(&dwc2_isr_calls, &dwc2_isr_cycles,
                    &dwc2_isr_max_cycles, cycles);
}

void wirelink_usb_cpu_record_dwc2_thread(uint32_t cycles) {
  record_cpu_cycles(&dwc2_thread_calls, &dwc2_thread_cycles,
                    &dwc2_thread_max_cycles, cycles);
}

void wirelink_usb_cpu_record_usbd_thread(uint32_t cycles) {
  record_cpu_cycles(&usbd_thread_calls, &usbd_thread_cycles,
                    &usbd_thread_max_cycles, cycles);
}

static uint32_t adapter_cycle_count(void *user_data) {
  (void)user_data;
  return wirelink_usb_cpu_cycle_count();
}

static void accumulate_u32(sample_u32_accumulator_t *accumulator,
                           uint32_t current) {
  accumulator->value += (uint32_t)(current - accumulator->previous);
  accumulator->previous = current;
}

static void snapshot_region(sample_cpu_region_accumulator_t *accumulator,
                            uint32_t calls, uint32_t total_cycles,
                            uint32_t max_cycles) {
  accumulate_u32(&accumulator->calls, calls);
  accumulate_u32(&accumulator->total_cycles, total_cycles);
  if (max_cycles > accumulator->max_cycles) {
    accumulator->max_cycles = max_cycles;
  }
}

static void snapshot_cpu_stats(void) {
  wl_zephyr_usb_bulk_stats_t current;

  snapshot_region(&dwc2_isr_stats, (uint32_t)atomic_get(&dwc2_isr_calls),
                  (uint32_t)atomic_get(&dwc2_isr_cycles),
                  (uint32_t)atomic_get(&dwc2_isr_max_cycles));
  snapshot_region(&dwc2_thread_stats, (uint32_t)atomic_get(&dwc2_thread_calls),
                  (uint32_t)atomic_get(&dwc2_thread_cycles),
                  (uint32_t)atomic_get(&dwc2_thread_max_cycles));
  snapshot_region(&usbd_thread_stats, (uint32_t)atomic_get(&usbd_thread_calls),
                  (uint32_t)atomic_get(&usbd_thread_cycles),
                  (uint32_t)atomic_get(&usbd_thread_max_cycles));

  wl_zephyr_usb_bulk_get_stats(&usb_adapter, &current);
  accumulate_u32(&adapter_stats.rx_claims, current.rx_claims);
  accumulate_u32(&adapter_stats.rx_completions, current.rx_completions);
  accumulate_u32(&adapter_stats.rx_bytes, current.rx_bytes);
  accumulate_u32(&adapter_stats.rx_pauses, current.rx_pauses);
  accumulate_u32(&adapter_stats.tx_submissions, current.tx_submissions);
  accumulate_u32(&adapter_stats.tx_completions, current.tx_completions);
  accumulate_u32(&adapter_stats.rx_callback_cycles, current.rx_callback_cycles);
  if (current.rx_callback_max_cycles > adapter_stats.rx_callback_max_cycles) {
    adapter_stats.rx_callback_max_cycles = current.rx_callback_max_cycles;
  }
  accumulate_u32(&adapter_stats.tx_callback_cycles, current.tx_callback_cycles);
  if (current.tx_callback_max_cycles > adapter_stats.tx_callback_max_cycles) {
    adapter_stats.tx_callback_max_cycles = current.tx_callback_max_cycles;
  }
  accumulate_u32(&adapter_stats.tx_sink_cycles, current.tx_sink_cycles);
  if (current.tx_sink_max_cycles > adapter_stats.tx_sink_max_cycles) {
    adapter_stats.tx_sink_max_cycles = current.tx_sink_max_cycles;
  }
  snapshot_region(&adapter_stats.active_service, current.active_service_calls,
                  current.active_service_cycles,
                  current.active_service_max_cycles);
  accumulate_u32(&adapter_stats.errors, current.errors);
}

static void print_accumulated_region(
    const char *name, const sample_cpu_region_accumulator_t *stats) {
  printk(",%s_calls,%llu,%s_total_cycles,%llu,%s_max_cycles,%u", name,
         (unsigned long long)stats->calls.value, name,
         (unsigned long long)stats->total_cycles.value, name,
         stats->max_cycles);
}

static void print_sample_region(const char *name,
                                const sample_cpu_region_stats_t *stats) {
  printk(",%s_calls,%llu,%s_total_cycles,%llu,%s_max_cycles,%u", name,
         (unsigned long long)stats->calls, name,
         (unsigned long long)stats->total_cycles, name, stats->max_cycles);
}

static void print_cpu_stats(void) {
  printk("wirelink_usb_cpu_v2,cpu_hz,%u,rx_claims,%llu,"
         "rx_completions,%llu,rx_bytes,%llu,rx_pauses,%llu,"
         "tx_submissions,%llu,tx_completions,%llu",
         (uint32_t)CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC,
         (unsigned long long)adapter_stats.rx_claims.value,
         (unsigned long long)adapter_stats.rx_completions.value,
         (unsigned long long)adapter_stats.rx_bytes.value,
         (unsigned long long)adapter_stats.rx_pauses.value,
         (unsigned long long)adapter_stats.tx_submissions.value,
         (unsigned long long)adapter_stats.tx_completions.value);
  print_accumulated_region("dwc2_isr", &dwc2_isr_stats);
  print_accumulated_region("dwc2_thread", &dwc2_thread_stats);
  print_accumulated_region("usbd_thread", &usbd_thread_stats);
  printk(",adapter_rx_callback_total_cycles,%llu,"
         "adapter_rx_callback_max_cycles,%u,"
         "adapter_tx_callback_total_cycles,%llu,"
         "adapter_tx_callback_max_cycles,%u,"
         "adapter_tx_sink_total_cycles,%llu,adapter_tx_sink_max_cycles,%u",
         (unsigned long long)adapter_stats.rx_callback_cycles.value,
         adapter_stats.rx_callback_max_cycles,
         (unsigned long long)adapter_stats.tx_callback_cycles.value,
         adapter_stats.tx_callback_max_cycles,
         (unsigned long long)adapter_stats.tx_sink_cycles.value,
         adapter_stats.tx_sink_max_cycles);
  print_accumulated_region("adapter_active_service",
                           &adapter_stats.active_service);
  print_sample_region("wl_poll", &wl_poll_stats);
  print_sample_region("rx_event_copy_release", &rx_event_copy_release_stats);
  print_sample_region("wl_send_unreliable", &wl_send_unreliable_stats);
  print_sample_region("wl_zephyr_usb_bulk_service", &usb_service_stats);
  printk(",errors,%llu\n", (unsigned long long)adapter_stats.errors.value);
}

#define SAMPLE_CPU_START(name)                                               \
  const uint32_t name = wirelink_usb_cpu_cycle_count()
#define SAMPLE_CPU_FINISH(stats, started)                                    \
  record_sample_cpu_cycles(&(stats),                                         \
                           wirelink_usb_cpu_cycle_count() - (started))
#define SAMPLE_CPU_FINISH_IF(stats, started, enabled)                         \
  do {                                                                       \
    const uint32_t sample_cpu_elapsed =                                      \
        wirelink_usb_cpu_cycle_count() - (started);                          \
    if (enabled) {                                                           \
      record_sample_cpu_cycles(&(stats), sample_cpu_elapsed);                \
    }                                                                        \
  } while (false)
#else
#define SAMPLE_CPU_START(name)
#define SAMPLE_CPU_FINISH(stats, started)
#define SAMPLE_CPU_FINISH_IF(stats, started, enabled)
#endif

static void usb_message(struct usbd_context *context,
                        const struct usbd_msg *message) {
  if (usbd_can_detect_vbus(context) && message->type == USBD_MSG_VBUS_READY) {
    (void)usbd_enable(context);
  }
  if (usbd_can_detect_vbus(context) &&
      message->type == USBD_MSG_VBUS_REMOVED) {
    (void)usbd_disable(context);
  }
}

int main(void) {
  const wl_config_t config = {
      .max_payload_len = MAX_PAYLOAD,
      .envelope = WL_ENVELOPE_COBS_STREAM,
      .integrity = WL_INTEGRITY_CRC32C,
      .session_id = UINT64_C(0x55534242554C4B31),
      .max_retries = 2U,
      .ack_timeout_ms = 20U,
      .max_transmission_unit = sizeof(tx_unit),
  };
  const wl_storage_t storage = {
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
  const wl_zephyr_usb_bulk_config_t adapter_config = {
      .link = &link,
      .maximum_rx_size = UNIT_STORAGE,
#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
      .cycle_counter = adapter_cycle_count,
#endif
  };
  struct usbd_context *usb_context;
  uint8_t pending_payload[MAX_PAYLOAD];
  size_t pending_payload_len = 0U;
  uint16_t pending_message_id = 0U;
  bool echo_pending = false;
#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
  int64_t next_stats_snapshot = 0;
  int64_t last_activity = 0;
  uint32_t observed_completions = 0U;
  uint64_t reported_completions = 0U;
  bool sample_window_active = false;
#endif
  int result;

  result = wl_init(&link, &config, &storage);
  if (result != WL_OK) {
    printk("wl_init failed: %d\n", result);
    return 0;
  }
  result = wl_zephyr_usb_bulk_init(&usb_adapter, &adapter_config);
  if (result != WL_OK) {
    printk("USB bulk adapter init failed: %d\n", result);
    return 0;
  }

  usb_context = sample_usbd_init_device(usb_message);
  if (usb_context == NULL) {
    printk("USB device init failed\n");
    return 0;
  }
  if (!usbd_can_detect_vbus(usb_context)) {
    result = usbd_enable(usb_context);
    if (result != 0) {
      printk("USB enable failed: %d\n", result);
      return 0;
    }
  }

  printk("Wirelink vendor bulk endpoint ready (OUT=0x01 IN=0x81 DMA=off)\n");
  for (;;) {
#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
    bool tx_completed_this_iteration = false;
    bool tx_submitted_this_iteration = false;
#endif

    if (!echo_pending) {
      wl_event_t pending_event;
#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
      uint32_t now_ms;
      uint32_t poll_cycles;
      bool received_payload;
#endif

      memset(&pending_event, 0, sizeof(pending_event));
#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
      now_ms = k_uptime_get_32();
      SAMPLE_CPU_START(poll_started);
      result = wl_poll(&link, now_ms, &pending_event);
      poll_cycles = wirelink_usb_cpu_cycle_count() - poll_started;
      received_payload =
          result == WL_OK &&
          (pending_event.type == WL_EVT_UNRELIABLE_RX ||
           pending_event.type == WL_EVT_RELIABLE_RX);
      if (sample_window_active || received_payload) {
        record_sample_cpu_cycles(&wl_poll_stats, poll_cycles);
      }
      if (received_payload) {
        sample_window_active = true;
      }
#else
      result = wl_poll(&link, k_uptime_get_32(), &pending_event);
#endif
      if (result == WL_OK &&
          (pending_event.type == WL_EVT_UNRELIABLE_RX ||
           pending_event.type == WL_EVT_RELIABLE_RX)) {
        SAMPLE_CPU_START(copy_release_started);
        pending_message_id = pending_event.message_id;
        pending_payload_len = pending_event.payload_len;
        memcpy(pending_payload, pending_event.payload, pending_payload_len);
        wl_event_release(&link, &pending_event);
        SAMPLE_CPU_FINISH(rx_event_copy_release_stats, copy_release_started);
        echo_pending = true;
      }
    }

    SAMPLE_CPU_START(service_started);
    (void)wl_zephyr_usb_bulk_service(&usb_adapter);
    SAMPLE_CPU_FINISH_IF(usb_service_stats, service_started,
                         sample_window_active);
#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
    {
      const uint32_t completions =
          (uint32_t)atomic_get(&usb_adapter.tx_completions);

      if (completions != observed_completions) {
        observed_completions = completions;
        last_activity = k_uptime_get();
        tx_completed_this_iteration = true;
      }
    }
#endif
    if (echo_pending) {
      SAMPLE_CPU_START(send_started);
      result = wl_send_unreliable(&link, pending_message_id, pending_payload,
                                  pending_payload_len);
      SAMPLE_CPU_FINISH(wl_send_unreliable_stats, send_started);
      if (result == WL_OK) {
        echo_pending = false;
#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
        tx_submitted_this_iteration = true;
#endif
      } else if (result != WL_ERR_BUSY) {
        echo_pending = false;
#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
        sample_window_active = false;
#endif
      }
    }
#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
    if (tx_completed_this_iteration && !tx_submitted_this_iteration &&
        !echo_pending) {
      sample_window_active = false;
    }

    if (k_uptime_get() >= next_stats_snapshot) {
      const int64_t now = k_uptime_get();

      snapshot_cpu_stats();
      next_stats_snapshot = now + CPU_STATS_SNAPSHOT_PERIOD_MS;
      if (adapter_stats.tx_completions.value != 0U &&
          adapter_stats.tx_completions.value != reported_completions &&
          now - last_activity >= 500) {
        print_cpu_stats();
        reported_completions = adapter_stats.tx_completions.value;
      }
    }
#endif
    k_yield();
  }
  return 0;
}

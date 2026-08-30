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

static wl_ctx_t link;
static wl_zephyr_usb_bulk_t usb_adapter;
static uint8_t tx_payload[MAX_PAYLOAD];
static uint8_t tx_unit[UNIT_STORAGE];
static uint8_t control_unit[64];
static uint8_t rx_fifo[RX_RING_STORAGE];
static uint8_t rx_fallback[UNIT_STORAGE];

#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
typedef struct sample_cpu_region_stats {
  uint32_t calls;
  uint32_t total_cycles;
  uint32_t max_cycles;
} sample_cpu_region_stats_t;

static atomic_t dwc2_isr_calls;
static atomic_t dwc2_isr_cycles;
static atomic_t dwc2_isr_max_cycles;
static atomic_t dwc2_thread_calls;
static atomic_t dwc2_thread_cycles;
static atomic_t dwc2_thread_max_cycles;
static atomic_t usbd_thread_calls;
static atomic_t usbd_thread_cycles;
static atomic_t usbd_thread_max_cycles;
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

static void print_atomic_region(const char *name, const atomic_t *calls,
                                const atomic_t *total,
                                const atomic_t *maximum) {
  printk(",%s_calls,%u,%s_total_cycles,%u,%s_max_cycles,%u", name,
         (uint32_t)atomic_get(calls), name, (uint32_t)atomic_get(total), name,
         (uint32_t)atomic_get(maximum));
}

static void print_sample_region(const char *name,
                                const sample_cpu_region_stats_t *stats) {
  printk(",%s_calls,%u,%s_total_cycles,%u,%s_max_cycles,%u", name,
         stats->calls, name, stats->total_cycles, name, stats->max_cycles);
}

static void print_cpu_stats(const wl_zephyr_usb_bulk_stats_t *stats) {
  printk("wirelink_usb_cpu_v2,cpu_hz,%u,rx_claims,%u,rx_completions,%u,"
         "rx_bytes,%u,rx_pauses,%u,tx_submissions,%u,tx_completions,%u",
         (uint32_t)CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC, stats->rx_claims,
         stats->rx_completions, stats->rx_bytes, stats->rx_pauses,
         stats->tx_submissions, stats->tx_completions);
  print_atomic_region("dwc2_isr", &dwc2_isr_calls, &dwc2_isr_cycles,
                      &dwc2_isr_max_cycles);
  print_atomic_region("dwc2_thread", &dwc2_thread_calls,
                      &dwc2_thread_cycles, &dwc2_thread_max_cycles);
  print_atomic_region("usbd_thread", &usbd_thread_calls,
                      &usbd_thread_cycles, &usbd_thread_max_cycles);
  printk(",adapter_rx_callback_total_cycles,%u,"
         "adapter_rx_callback_max_cycles,%u,"
         "adapter_tx_callback_total_cycles,%u,"
         "adapter_tx_callback_max_cycles,%u,"
         "adapter_tx_sink_total_cycles,%u,adapter_tx_sink_max_cycles,%u,"
         "adapter_active_service_calls,%u,"
         "adapter_active_service_total_cycles,%u,"
         "adapter_active_service_max_cycles,%u",
         stats->rx_callback_cycles, stats->rx_callback_max_cycles,
         stats->tx_callback_cycles, stats->tx_callback_max_cycles,
         stats->tx_sink_cycles, stats->tx_sink_max_cycles,
         stats->active_service_calls, stats->active_service_cycles,
         stats->active_service_max_cycles);
  print_sample_region("wl_poll", &wl_poll_stats);
  print_sample_region("rx_event_copy_release", &rx_event_copy_release_stats);
  print_sample_region("wl_send_unreliable", &wl_send_unreliable_stats);
  print_sample_region("wl_zephyr_usb_bulk_service", &usb_service_stats);
  printk(",errors,%u\n", stats->errors);
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
  int64_t next_stats_check = 0;
  int64_t last_activity = 0;
  uint32_t observed_completions = 0U;
  uint32_t reported_completions = 0U;
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

    if (k_uptime_get() >= next_stats_check) {
      const int64_t now = k_uptime_get();

      next_stats_check = now + 100;
      if (observed_completions != 0U &&
          observed_completions != reported_completions &&
          now - last_activity >= 500) {
        wl_zephyr_usb_bulk_stats_t stats;

        wl_zephyr_usb_bulk_get_stats(&usb_adapter, &stats);
        print_cpu_stats(&stats);
        reported_completions = observed_completions;
      }
    }
#endif
    k_yield();
  }
  return 0;
}

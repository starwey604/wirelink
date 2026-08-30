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
static atomic_t dwc2_isr_calls;
static atomic_t dwc2_isr_cycles;
static atomic_t dwc2_isr_max_cycles;
static atomic_t dwc2_thread_calls;
static atomic_t dwc2_thread_cycles;
static atomic_t dwc2_thread_max_cycles;
static atomic_t usbd_thread_calls;
static atomic_t usbd_thread_cycles;
static atomic_t usbd_thread_max_cycles;

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

static void print_cpu_stats(const wl_zephyr_usb_bulk_stats_t *stats) {
  printk("wirelink_usb_cpu_v1,cpu_hz,240000000,rx,%u,tx,%u,rx_bytes,%u,"
         "isr_calls,%u,isr_cycles,%u,isr_max,%u,dwc2_calls,%u,"
         "dwc2_cycles,%u,dwc2_max,%u,usbd_calls,%u,usbd_cycles,%u,"
         "usbd_max,%u,rx_cb_cycles,%u,rx_cb_max,%u,tx_cb_cycles,%u,"
         "tx_cb_max,%u,tx_sink_cycles,%u,tx_sink_max,%u,service_calls,%u,"
         "service_cycles,%u,service_max,%u,errors,%u\n",
         stats->rx_completions, stats->tx_completions, stats->rx_bytes,
         (uint32_t)atomic_get(&dwc2_isr_calls),
         (uint32_t)atomic_get(&dwc2_isr_cycles),
         (uint32_t)atomic_get(&dwc2_isr_max_cycles),
         (uint32_t)atomic_get(&dwc2_thread_calls),
         (uint32_t)atomic_get(&dwc2_thread_cycles),
         (uint32_t)atomic_get(&dwc2_thread_max_cycles),
         (uint32_t)atomic_get(&usbd_thread_calls),
         (uint32_t)atomic_get(&usbd_thread_cycles),
         (uint32_t)atomic_get(&usbd_thread_max_cycles),
         stats->rx_callback_cycles, stats->rx_callback_max_cycles,
         stats->tx_callback_cycles, stats->tx_callback_max_cycles,
         stats->tx_sink_cycles, stats->tx_sink_max_cycles,
         stats->active_service_calls, stats->active_service_cycles,
         stats->active_service_max_cycles, stats->errors);
}
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
    if (!echo_pending) {
      wl_event_t pending_event;

      memset(&pending_event, 0, sizeof(pending_event));
      result = wl_poll(&link, k_uptime_get_32(), &pending_event);
      if (result == WL_OK &&
          (pending_event.type == WL_EVT_UNRELIABLE_RX ||
           pending_event.type == WL_EVT_RELIABLE_RX)) {
        pending_message_id = pending_event.message_id;
        pending_payload_len = pending_event.payload_len;
        memcpy(pending_payload, pending_event.payload, pending_payload_len);
        wl_event_release(&link, &pending_event);
        echo_pending = true;
      }
    }

    (void)wl_zephyr_usb_bulk_service(&usb_adapter);
    if (echo_pending) {
      result = wl_send_unreliable(&link, pending_message_id, pending_payload,
                                  pending_payload_len);
      if (result == WL_OK) {
        echo_pending = false;
      } else if (result != WL_ERR_BUSY) {
        echo_pending = false;
      }
    }
#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
    if (k_uptime_get() >= next_stats_check) {
      wl_zephyr_usb_bulk_stats_t stats;
      const int64_t now = k_uptime_get();

      wl_zephyr_usb_bulk_get_stats(&usb_adapter, &stats);
      next_stats_check = now + 100;
      if (stats.tx_completions != observed_completions) {
        observed_completions = stats.tx_completions;
        last_activity = now;
      } else if (observed_completions != 0U &&
                 observed_completions != reported_completions &&
                 now - last_activity >= 500) {
        print_cpu_stats(&stats);
        reported_completions = observed_completions;
      }
    }
#endif
    k_yield();
  }
  return 0;
}

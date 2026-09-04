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

#include "control.h"
#include "crc_internal.h"
#include "wirelink/bulk.h"
#include "wirelink/frame.h"
#include "wirelink/wirelink.h"
#include "wirelink/zephyr/usb_bulk.h"

#define MAX_PAYLOAD WL_FRAME_MAX_PAYLOAD
#define UNIT_STORAGE WL_FRAME_MAX_COBS_LEN
#define RX_RING_STORAGE (2U * UNIT_STORAGE)
#define OBJECT_MAX_LENGTH (UINT64_C(64) * UINT64_C(1024) * UINT64_C(1024))
#define OBJECT_CHUNK_SIZE 2016U
#define OBJECT_IDLE_TIMEOUT_MS 1000U
#define OBJECT_REPORT_DELAY_MS 100U
#define CPU_STATS_SNAPSHOT_PERIOD_MS 100

_Static_assert(OBJECT_CHUNK_SIZE + 32U <= MAX_PAYLOAD,
               "bulk schema headroom exceeds Wirelink payload capacity");
_Static_assert(CONTROL_BULK_PHASE_BEGIN == WL_BULK_PHASE_BEGIN &&
                   CONTROL_BULK_PHASE_CHUNK == WL_BULK_PHASE_CHUNK &&
                   CONTROL_BULK_PHASE_END == WL_BULK_PHASE_END &&
                   CONTROL_BULK_PHASE_ABORT == WL_BULK_PHASE_ABORT,
               "generated and runtime bulk phase values differ");
_Static_assert(CONTROL_BULK_STATUS_TIMED_OUT == WL_BULK_STATUS_TIMED_OUT,
               "generated and runtime bulk status values differ");

static wl_ctx_t link;
static wl_zephyr_usb_bulk_t usb_adapter;
static wl_bulk_receiver_t object_receiver;
static uint8_t tx_payload[MAX_PAYLOAD];
static uint8_t tx_unit[UNIT_STORAGE];
static uint8_t control_unit[64];
static uint8_t rx_fifo[RX_RING_STORAGE];
static uint8_t rx_fallback[UNIT_STORAGE];
static uint8_t echo_payload[MAX_PAYLOAD];
static uint8_t status_payload[64];

typedef struct object_sink_state {
  uint32_t transfer_id;
  uint32_t expected_crc32c;
  uint32_t crc_state;
  uint32_t started_at_ms;
  uint32_t elapsed_ms;
  uint32_t write_calls;
  uint64_t total_length;
  uint64_t next_offset;
  uint32_t completed_transfer_id;
  uint32_t completed_crc32c;
  uint32_t completed_write_calls;
  uint32_t report_ready_at_ms;
  uint64_t completed_length;
  bool active;
  bool report_pending;
} object_sink_state_t;

static object_sink_state_t object_sink;

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
static sample_cpu_region_stats_t bulk_dispatch_stats;
static sample_cpu_region_stats_t bulk_sink_write_stats;
static sample_cpu_region_stats_t bulk_status_send_stats;

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
  printk("wirelink_usb_cpu_v3,cpu_hz,%u,rx_claims,%llu,"
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
  print_sample_region("bulk_dispatch", &bulk_dispatch_stats);
  print_sample_region("bulk_sink_write", &bulk_sink_write_stats);
  print_sample_region("bulk_status_send", &bulk_status_send_stats);
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

static uint8_t object_pattern_byte(uint8_t index) {
  return index ^ UINT8_C(0xa5);
}

static wl_bulk_sink_result_t
object_begin(void *user_data, const wl_bulk_descriptor_t *descriptor,
             uint64_t *out_resume_offset) {
  object_sink_state_t *state = user_data;

  if (state == NULL || descriptor == NULL || out_resume_offset == NULL ||
      descriptor->transfer_id == 0U || descriptor->total_length == 0U ||
      descriptor->total_length > OBJECT_MAX_LENGTH) {
    return WL_BULK_SINK_INVALID;
  }
  state->transfer_id = descriptor->transfer_id;
  state->expected_crc32c = descriptor->object_crc32c;
  state->crc_state = UINT32_MAX;
  state->started_at_ms = k_uptime_get_32();
  state->write_calls = 0U;
  state->total_length = descriptor->total_length;
  state->next_offset = 0U;
  state->active = true;
  *out_resume_offset = 0U;
  return WL_BULK_SINK_OK;
}

static wl_bulk_sink_result_t object_write(void *user_data,
                                          uint32_t transfer_id,
                                          uint64_t offset,
                                          const uint8_t *data,
                                          size_t length) {
  object_sink_state_t *state = user_data;
  size_t index;
  uint8_t pattern_index = (uint8_t)offset;

  SAMPLE_CPU_START(write_started);
  if (state == NULL || !state->active || data == NULL || length == 0U ||
      transfer_id != state->transfer_id || offset != state->next_offset ||
      state->next_offset > state->total_length ||
      length > state->total_length - state->next_offset) {
    SAMPLE_CPU_FINISH(bulk_sink_write_stats, write_started);
    return WL_BULK_SINK_INVALID;
  }
  for (index = 0U; index < length; ++index) {
    if (data[index] != object_pattern_byte(pattern_index)) {
      SAMPLE_CPU_FINISH(bulk_sink_write_stats, write_started);
      return WL_BULK_SINK_INTEGRITY_FAILED;
    }
    pattern_index++;
  }
  state->crc_state = wl_crc32c_update(state->crc_state, data, length);
  state->next_offset += length;
  state->write_calls++;
  SAMPLE_CPU_FINISH(bulk_sink_write_stats, write_started);
  return WL_BULK_SINK_OK;
}

static wl_bulk_sink_result_t
object_finish(void *user_data, const wl_bulk_descriptor_t *descriptor) {
  object_sink_state_t *state = user_data;
  uint32_t actual_crc32c;

  if (state == NULL || descriptor == NULL || !state->active ||
      descriptor->transfer_id != state->transfer_id ||
      descriptor->total_length != state->total_length ||
      state->next_offset != state->total_length) {
    return WL_BULK_SINK_INVALID;
  }
  actual_crc32c = state->crc_state ^ UINT32_MAX;
  if (actual_crc32c != state->expected_crc32c ||
      actual_crc32c != descriptor->object_crc32c) {
    return WL_BULK_SINK_INTEGRITY_FAILED;
  }

  state->active = false;
  state->completed_transfer_id = state->transfer_id;
  state->completed_length = state->total_length;
  state->completed_crc32c = actual_crc32c;
  state->completed_write_calls = state->write_calls;
  state->elapsed_ms = k_uptime_get_32() - state->started_at_ms;
  state->report_ready_at_ms =
      k_uptime_get_32() + OBJECT_REPORT_DELAY_MS;
  state->report_pending = true;
  return WL_BULK_SINK_OK;
}

static void object_abort(void *user_data, uint32_t transfer_id,
                         int32_t reason) {
  object_sink_state_t *state = user_data;

  (void)reason;
  if (state != NULL && state->active && state->transfer_id == transfer_id) {
    state->active = false;
  }
}

static bool is_bulk_message(uint16_t message_id) {
  return message_id >= BULK_BEGIN_MESSAGE_ID &&
         message_id <= BULK_ABORT_MESSAGE_ID;
}

static void dispatch_bulk_event(const wl_event_t *event, uint32_t now_ms) {
  wl_bulk_err_t result = WL_BULK_ERR_INVALID_ARG;

  SAMPLE_CPU_START(dispatch_started);
  switch (event->message_id) {
  case BULK_BEGIN_MESSAGE_ID: {
    bulk_begin_t message = {0};
    wl_bulk_descriptor_t descriptor;

    if (bulk_begin_decode(event->payload, event->payload_len, &message) !=
            WL_CODEC_OK ||
        !message.has_transfer_id || !message.has_total_length ||
        !message.has_requested_chunk_size || !message.has_object_crc32c) {
      break;
    }
    descriptor = (wl_bulk_descriptor_t){
        .transfer_id = message.transfer_id,
        .total_length = message.total_length,
        .requested_chunk_size = message.requested_chunk_size,
        .object_crc32c = message.object_crc32c,
    };
    result = wl_bulk_receiver_on_begin(&object_receiver, &descriptor, now_ms);
    break;
  }
  case BULK_CHUNK_MESSAGE_ID: {
    bulk_chunk_t message = {0};
    wl_bulk_chunk_t chunk;

    if (bulk_chunk_decode(event->payload, event->payload_len, &message) !=
            WL_CODEC_OK ||
        !message.has_transfer_id || !message.has_offset || !message.has_data) {
      break;
    }
    chunk = (wl_bulk_chunk_t){
        .transfer_id = message.transfer_id,
        .offset = message.offset,
        .data = message.data.data,
        .length = message.data.length,
    };
    result = wl_bulk_receiver_on_chunk(&object_receiver, &chunk, now_ms);
    break;
  }
  case BULK_END_MESSAGE_ID: {
    bulk_end_t message = {0};

    if (bulk_end_decode(event->payload, event->payload_len, &message) !=
            WL_CODEC_OK ||
        !message.has_transfer_id || !message.has_total_length ||
        !message.has_object_crc32c) {
      break;
    }
    result = wl_bulk_receiver_on_end(&object_receiver, message.transfer_id,
                                     message.total_length,
                                     message.object_crc32c, now_ms);
    break;
  }
  case BULK_ABORT_MESSAGE_ID: {
    bulk_abort_t message = {0};

    if (bulk_abort_decode(event->payload, event->payload_len, &message) !=
            WL_CODEC_OK ||
        !message.has_transfer_id || !message.has_reason) {
      break;
    }
    result = wl_bulk_receiver_on_abort(&object_receiver, message.transfer_id,
                                       message.reason, now_ms);
    break;
  }
  default:
    break;
  }
  SAMPLE_CPU_FINISH(bulk_dispatch_stats, dispatch_started);
  if (result != WL_BULK_OK) {
    printk("bulk dispatch failed: id=%u error=%d\n", event->message_id,
           (int)result);
  }
}

/* Returns true while a retained Status still prevents additional bulk input. */
static bool service_bulk_status(void) {
  wl_bulk_receiver_status_view_t view;
  bulk_status_t message;
  size_t encoded_length = 0U;
  wl_bulk_err_t bulk_result;
  int send_result;

  bulk_result = wl_bulk_receiver_status_acquire(&object_receiver, &view);
  if (bulk_result == WL_BULK_ERR_NOT_FOUND) {
    return false;
  }
  if (bulk_result != WL_BULK_OK) {
    return true;
  }
  message = (bulk_status_t){
      .has_transfer_id = true,
      .transfer_id = view.status.transfer_id,
      .has_phase = true,
      .phase = view.status.phase,
      .has_code = true,
      .code = view.status.code,
      .has_next_offset = true,
      .next_offset = view.status.next_offset,
      .has_accepted_chunk_size = true,
      .accepted_chunk_size = view.status.accepted_chunk_size,
  };
  if (bulk_status_encode(&message, status_payload, sizeof(status_payload),
                         &encoded_length) != WL_CODEC_OK) {
    (void)wl_bulk_receiver_status_defer(&object_receiver, &view);
    return true;
  }

  SAMPLE_CPU_START(status_send_started);
  send_result = wl_send_unreliable(&link, BULK_STATUS_MESSAGE_ID,
                                   status_payload, encoded_length);
  SAMPLE_CPU_FINISH(bulk_status_send_stats, status_send_started);
  if (send_result == WL_OK) {
    (void)wl_bulk_receiver_status_release(&object_receiver, &view);
    return false;
  }
  (void)wl_bulk_receiver_status_defer(&object_receiver, &view);
  if (send_result != WL_ERR_BUSY) {
    printk("bulk Status send failed: %d\n", send_result);
  }
  return true;
}

static void maybe_print_object_report(void) {
  wl_zephyr_usb_bulk_stats_t stats;
  wl_bulk_receiver_state_t receiver_state;
  uint64_t next_offset;

  if (!object_sink.report_pending ||
      (int32_t)(k_uptime_get_32() - object_sink.report_ready_at_ms) < 0) {
    return;
  }
  if (wl_bulk_receiver_get_state(&object_receiver, &receiver_state,
                                 &next_offset) != WL_BULK_OK ||
      receiver_state == WL_BULK_RECEIVER_RECEIVING) {
    return;
  }
  (void)next_offset;
  wl_zephyr_usb_bulk_get_stats(&usb_adapter, &stats);
  if (stats.tx_active) {
    return;
  }
  printk("wirelink_object_rx_v1,transfer_id,%u,bytes,%llu,elapsed_ms,%u,"
         "crc32c,0x%08x,write_calls,%u\n",
         object_sink.completed_transfer_id,
         (unsigned long long)object_sink.completed_length,
         object_sink.elapsed_ms, object_sink.completed_crc32c,
         object_sink.completed_write_calls);
  object_sink.report_pending = false;
}

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
      .integrity = WL_INTEGRITY_NONE,
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
  const wl_bulk_receiver_config_t receiver_config = {
      .max_object_length = OBJECT_MAX_LENGTH,
      .max_chunk_size = OBJECT_CHUNK_SIZE,
      .write_alignment = 1U,
      .idle_timeout_ms = OBJECT_IDLE_TIMEOUT_MS,
      .sink =
          {
              .user_data = &object_sink,
              .begin = object_begin,
              .write = object_write,
              .finish = object_finish,
              .abort = object_abort,
          },
  };
  struct usbd_context *usb_context;
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
  result = wl_bulk_receiver_init(&object_receiver, &receiver_config);
  if (result != WL_BULK_OK) {
    printk("bulk receiver init failed: %d\n", result);
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

  printk("Wirelink vendor bulk endpoint ready (OUT=0x01 IN=0x81 DMA=off "
         "payload=%u object_chunk=%u)\n",
         MAX_PAYLOAD, OBJECT_CHUNK_SIZE);
  for (;;) {
    bool bulk_status_pending;
#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
    bool tx_completed_this_iteration = false;
    bool tx_submitted_this_iteration = false;
#endif

    bulk_status_pending = service_bulk_status();
    if (!echo_pending && !bulk_status_pending) {
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
        if (is_bulk_message(pending_event.message_id)) {
          dispatch_bulk_event(&pending_event, k_uptime_get_32());
          wl_event_release(&link, &pending_event);
        } else {
          SAMPLE_CPU_START(copy_release_started);
          pending_message_id = pending_event.message_id;
          pending_payload_len = pending_event.payload_len;
          memcpy(echo_payload, pending_event.payload, pending_payload_len);
          echo_pending = true;
          wl_event_release(&link, &pending_event);
          SAMPLE_CPU_FINISH(rx_event_copy_release_stats,
                            copy_release_started);
        }
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
      result = wl_send_unreliable(&link, pending_message_id, echo_payload,
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
    (void)wl_bulk_receiver_poll(&object_receiver, k_uptime_get_32());
    maybe_print_object_report();
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

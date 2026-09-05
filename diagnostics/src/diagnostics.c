/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/diagnostics.h"

#include <stdint.h>

static void wl_diag_terminate(wl_diag_writer_t *writer) {
  if (writer->capacity != 0U)
    writer->data[writer->length] = '\0';
}

static void wl_diag_append_char(wl_diag_writer_t *writer, char value) {
  if (writer->capacity > 0U && writer->length + 1U < writer->capacity)
    writer->data[writer->length++] = value;
  ++writer->required;
  wl_diag_terminate(writer);
}

static void wl_diag_append_text(wl_diag_writer_t *writer, const char *text) {
  while (*text != '\0')
    wl_diag_append_char(writer, *text++);
}

static void wl_diag_append_u64(wl_diag_writer_t *writer, uint64_t value) {
  char digits[20];
  size_t count = 0U;
  do {
    digits[count++] = (char)('0' + value % UINT64_C(10));
    value /= UINT64_C(10);
  } while (value != 0U);
  while (count != 0U)
    wl_diag_append_char(writer, digits[--count]);
}

static void wl_diag_append_i64(wl_diag_writer_t *writer, int64_t value) {
  uint64_t magnitude;
  if (value < 0) {
    wl_diag_append_char(writer, '-');
    magnitude = (uint64_t)(-(value + 1)) + UINT64_C(1);
  } else {
    magnitude = (uint64_t)value;
  }
  wl_diag_append_u64(writer, magnitude);
}

static void wl_diag_field_u64(wl_diag_writer_t *writer, const char *name,
                              uint64_t value) {
  wl_diag_append_char(writer, ' ');
  wl_diag_append_text(writer, name);
  wl_diag_append_char(writer, '=');
  wl_diag_append_u64(writer, value);
}

static void wl_diag_field_i64(wl_diag_writer_t *writer, const char *name,
                              int64_t value) {
  wl_diag_append_char(writer, ' ');
  wl_diag_append_text(writer, name);
  wl_diag_append_char(writer, '=');
  wl_diag_append_i64(writer, value);
}

static wl_err_t wl_diag_result(const wl_diag_writer_t *writer) {
  return writer->required < writer->capacity ? WL_OK : WL_ERR_BUF_TOO_SMALL;
}

static wl_err_t wl_diag_begin(wl_diag_writer_t *writer, const void *value,
                              const char *record) {
  if (writer == NULL || value == NULL ||
      (writer->capacity != 0U && writer->data == NULL) ||
      (writer->capacity != 0U && writer->length >= writer->capacity))
    return WL_ERR_INVALID_ARG;
  wl_diag_append_text(writer, record);
  return WL_OK;
}

wl_err_t wl_diag_writer_init(wl_diag_writer_t *writer, char *data,
                             size_t capacity) {
  if (writer == NULL || (capacity != 0U && data == NULL))
    return WL_ERR_INVALID_ARG;
  writer->data = data;
  writer->capacity = capacity;
  writer->length = 0U;
  writer->required = 0U;
  wl_diag_terminate(writer);
  return WL_OK;
}

void wl_diag_writer_reset(wl_diag_writer_t *writer) {
  if (writer == NULL)
    return;
  writer->length = 0U;
  writer->required = 0U;
  if (writer->capacity != 0U && writer->data != NULL)
    writer->data[0] = '\0';
}

wl_err_t wl_diag_format_rx_counters(wl_diag_writer_t *writer,
                                    const wl_rx_counters_t *counters) {
  if (wl_diag_begin(writer, counters, "rx") != WL_OK)
    return WL_ERR_INVALID_ARG;
  wl_diag_field_u64(writer, "malformed", counters->malformed);
  wl_diag_field_u64(writer, "bad_integrity", counters->bad_integrity);
  wl_diag_field_u64(writer, "overflow", counters->overflow);
  wl_diag_field_u64(writer, "duplicate", counters->duplicate);
  wl_diag_field_u64(writer, "unsupported", counters->unsupported);
  wl_diag_append_char(writer, '\n');
  return wl_diag_result(writer);
}

wl_err_t wl_diag_format_adapter_stats(wl_diag_writer_t *writer,
                                      const wl_adapter_stats_t *stats) {
  if (wl_diag_begin(writer, stats, "adapter") != WL_OK)
    return WL_ERR_INVALID_ARG;
  wl_diag_field_u64(writer, "rx_units", stats->rx_units);
  wl_diag_field_u64(writer, "rx_bytes", stats->rx_bytes);
  wl_diag_field_u64(writer, "rx_backpressure", stats->rx_backpressure);
  wl_diag_field_u64(writer, "tx_units", stats->tx_units);
  wl_diag_field_u64(writer, "tx_bytes", stats->tx_bytes);
  wl_diag_field_u64(writer, "tx_completions", stats->tx_completions);
  wl_diag_field_u64(writer, "activity_notifications",
                    stats->activity_notifications);
  wl_diag_field_u64(writer, "service_calls", stats->service_calls);
  wl_diag_field_u64(writer, "errors", stats->errors);
  wl_diag_field_u64(writer, "started", stats->started);
  wl_diag_field_u64(writer, "rx_paused", stats->rx_paused);
  wl_diag_field_u64(writer, "tx_active", stats->tx_active);
  wl_diag_append_char(writer, '\n');
  return wl_diag_result(writer);
}

wl_err_t wl_diag_format_fifo_stats(wl_diag_writer_t *writer,
                                   const wl_fifo_stats_t *stats) {
  if (wl_diag_begin(writer, stats, "fifo") != WL_OK)
    return WL_ERR_INVALID_ARG;
  wl_diag_field_u64(writer, "depth", stats->depth);
  wl_diag_field_u64(writer, "high_watermark", stats->high_watermark);
  wl_diag_field_u64(writer, "publishes", stats->publishes);
  wl_diag_field_u64(writer, "consumes", stats->consumes);
  wl_diag_field_u64(writer, "full_rejections", stats->full_rejections);
  wl_diag_field_u64(writer, "empty_reads", stats->empty_reads);
  wl_diag_field_u64(writer, "aborts", stats->aborts);
  wl_diag_field_u64(writer, "resets", stats->resets);
  wl_diag_field_u64(writer, "errors", stats->errors);
  wl_diag_append_char(writer, '\n');
  return wl_diag_result(writer);
}

wl_err_t wl_diag_format_latest_stats(wl_diag_writer_t *writer,
                                     const wl_latest_stats_t *stats) {
  if (wl_diag_begin(writer, stats, "latest") != WL_OK)
    return WL_ERR_INVALID_ARG;
  wl_diag_field_u64(writer, "generation", stats->generation);
  wl_diag_field_u64(writer, "publishes", stats->publishes);
  wl_diag_field_u64(writer, "reads", stats->reads);
  wl_diag_field_u64(writer, "coalesced", stats->coalesced);
  wl_diag_field_u64(writer, "empty_reads", stats->empty_reads);
  wl_diag_field_u64(writer, "resets", stats->resets);
  wl_diag_field_u64(writer, "errors", stats->errors);
  wl_diag_append_char(writer, '\n');
  return wl_diag_result(writer);
}

wl_err_t wl_diag_format_outbox_stats(wl_diag_writer_t *writer,
                                     const wl_outbox_stats_t *stats) {
  if (wl_diag_begin(writer, stats, "outbox") != WL_OK)
    return WL_ERR_INVALID_ARG;
  wl_diag_field_u64(writer, "submitted", stats->submitted);
  wl_diag_field_u64(writer, "coalesced", stats->coalesced);
  wl_diag_field_u64(writer, "queue_full", stats->queue_full);
  wl_diag_field_u64(writer, "acquired", stats->acquired);
  wl_diag_field_u64(writer, "accepted", stats->accepted);
  wl_diag_field_u64(writer, "deferred", stats->deferred);
  wl_diag_field_u64(writer, "rejected", stats->rejected);
  wl_diag_field_u64(writer, "superseded", stats->superseded);
  wl_diag_field_u64(writer, "resets", stats->resets);
  wl_diag_field_u64(writer, "depth", stats->depth);
  wl_diag_append_char(writer, '\n');
  return wl_diag_result(writer);
}

wl_err_t
wl_diag_format_bulk_receiver_stats(wl_diag_writer_t *writer,
                                   const wl_bulk_receiver_stats_t *stats) {
  if (wl_diag_begin(writer, stats, "bulk_receiver") != WL_OK)
    return WL_ERR_INVALID_ARG;
  wl_diag_field_u64(writer, "begins", stats->begins);
  wl_diag_field_u64(writer, "chunks", stats->chunks);
  wl_diag_field_u64(writer, "bytes_written", stats->bytes_written);
  wl_diag_field_u64(writer, "duplicate_messages", stats->duplicate_messages);
  wl_diag_field_u64(writer, "busy_responses", stats->busy_responses);
  wl_diag_field_u64(writer, "protocol_errors", stats->protocol_errors);
  wl_diag_field_u64(writer, "write_failures", stats->write_failures);
  wl_diag_field_u64(writer, "integrity_failures", stats->integrity_failures);
  wl_diag_field_u64(writer, "aborts", stats->aborts);
  wl_diag_field_u64(writer, "timeouts", stats->timeouts);
  wl_diag_append_char(writer, '\n');
  return wl_diag_result(writer);
}

wl_err_t wl_diag_format_bulk_sender_stats(wl_diag_writer_t *writer,
                                          const wl_bulk_sender_stats_t *stats) {
  if (wl_diag_begin(writer, stats, "bulk_sender") != WL_OK)
    return WL_ERR_INVALID_ARG;
  wl_diag_field_u64(writer, "starts", stats->starts);
  wl_diag_field_u64(writer, "actions_submitted", stats->actions_submitted);
  wl_diag_field_u64(writer, "statuses_received", stats->statuses_received);
  wl_diag_field_u64(writer, "retries", stats->retries);
  wl_diag_field_u64(writer, "busy_responses", stats->busy_responses);
  wl_diag_field_u64(writer, "protocol_errors", stats->protocol_errors);
  wl_diag_field_u64(writer, "completed", stats->completed);
  wl_diag_field_u64(writer, "failed", stats->failed);
  wl_diag_field_u64(writer, "aborted", stats->aborted);
  wl_diag_append_char(writer, '\n');
  return wl_diag_result(writer);
}

wl_err_t
wl_diag_format_rpc_client_result(wl_diag_writer_t *writer,
                                 const wl_rpc_client_result_t *result) {
  if (wl_diag_begin(writer, result, "rpc_client") != WL_OK)
    return WL_ERR_INVALID_ARG;
  wl_diag_field_u64(writer, "operation_id", result->operation_id);
  wl_diag_field_u64(writer, "request_message_id", result->request_message_id);
  wl_diag_field_u64(writer, "response_message_id", result->response_message_id);
  wl_diag_field_i64(writer, "state", result->state);
  wl_diag_field_u64(writer, "tx_handle", result->tx_handle);
  wl_diag_field_i64(writer, "link_result", result->link_result);
  wl_diag_field_i64(writer, "application_status", result->application_status);
  wl_diag_field_i64(writer, "runtime_error", result->runtime_error);
  wl_diag_field_u64(writer, "link_delivery_confirmed",
                    result->link_delivery_confirmed);
  wl_diag_field_u64(writer, "response_length", result->response_length);
  wl_diag_append_char(writer, '\n');
  return wl_diag_result(writer);
}

wl_err_t wl_diag_format_rpc_peer_observation(
    wl_diag_writer_t *writer, const wl_rpc_peer_observation_t *observation) {
  if (wl_diag_begin(writer, observation, "rpc_peer") != WL_OK)
    return WL_ERR_INVALID_ARG;
  wl_diag_field_u64(writer, "previous_session_id",
                    observation->previous_session_id);
  wl_diag_field_u64(writer, "current_session_id",
                    observation->current_session_id);
  wl_diag_field_u64(writer, "pending_discarded",
                    observation->discarded.pending_discarded);
  wl_diag_field_u64(writer, "responses_discarded",
                    observation->discarded.responses_discarded);
  wl_diag_field_u64(writer, "tx_cancel_requested",
                    observation->discarded.tx_cancel_requested);
  wl_diag_field_u64(writer, "changed", observation->changed);
  wl_diag_append_char(writer, '\n');
  return wl_diag_result(writer);
}

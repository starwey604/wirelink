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

wl_err_t wl_diag_format_kernel_result(wl_diag_writer_t *writer,
                                      const wl_kernel_result_t *result) {
  if (wl_diag_begin(writer, result, "kernel_result") != WL_OK)
    return WL_ERR_INVALID_ARG;
  wl_diag_field_u64(writer, "outcome", result->outcome);
  wl_diag_field_i64(writer, "status", result->status);
  wl_diag_append_char(writer, '\n');
  return wl_diag_result(writer);
}

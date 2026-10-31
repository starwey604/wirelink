/* SPDX-License-Identifier: Apache-2.0 */
#include "application.h"
#include "crc_internal.h"
#include <string.h>

static wl_err_t status(usb_sample_application_t *app, uint32_t id, int phase, int code) {
  bulk_status_t value = {.has_transfer_id = true, .transfer_id = id,
    .has_phase = true, .phase = phase, .has_code = true, .code = code,
    .has_next_offset = true, .next_offset = app->next,
    .has_accepted_chunk_size = true, .accepted_chunk_size = app->chunk_size};
  if (bulk_status_encode(&value, app->pending, sizeof(app->pending), &app->pending_length) != WL_CODEC_OK)
    return WL_ERR_CORRUPT_PAYLOAD;
  app->pending_id = BULK_STATUS_MESSAGE_ID; app->output_pending = true; return WL_OK;
}
static wl_err_t begin(usb_sample_application_t *app, const wl_kernel_frame_t *frame) {
  bulk_begin_t value;
  if (bulk_begin_decode(frame->payload, frame->length, &value) != WL_CODEC_OK ||
      !value.has_transfer_id || !value.has_total_length || !value.has_requested_chunk_size ||
      !value.has_object_crc32c) return WL_ERR_CORRUPT_PAYLOAD;
  int code = CONTROL_BULK_STATUS_OK;
  if (!value.transfer_id || !value.total_length || value.total_length > USB_SAMPLE_OBJECT_MAX ||
      !value.requested_chunk_size) code = CONTROL_BULK_STATUS_INVALID;
  else if (app->active) {
    if (value.transfer_id != app->transfer_id) code = CONTROL_BULK_STATUS_BUSY;
    else if (value.total_length != app->total || value.object_crc32c != app->expected_crc)
      code = CONTROL_BULK_STATUS_CONFLICT;
    else app->touched_at = app->now;
  } else if (value.transfer_id == app->completed_id) {
    if (value.total_length != app->completed_length || value.object_crc32c != app->completed_crc)
      code = CONTROL_BULK_STATUS_CONFLICT;
    else app->next = app->completed_length;
  } else {
    app->active = true; app->transfer_id = value.transfer_id; app->total = value.total_length;
    app->expected_crc = value.object_crc32c; app->crc = UINT32_MAX; app->next = 0;
    app->chunk_size = value.requested_chunk_size < USB_SAMPLE_CHUNK_MAX ? value.requested_chunk_size : USB_SAMPLE_CHUNK_MAX;
    app->writes = 0; app->started_at = app->touched_at = app->now;
  }
  return status(app, value.transfer_id, CONTROL_BULK_PHASE_BEGIN, code);
}
static wl_err_t chunk(usb_sample_application_t *app, const wl_kernel_frame_t *frame) {
  bulk_chunk_t value;
  if (bulk_chunk_decode(frame->payload, frame->length, &value) != WL_CODEC_OK ||
      !value.has_transfer_id || !value.has_offset || !value.has_data)
    return WL_ERR_CORRUPT_PAYLOAD;
  int code = CONTROL_BULK_STATUS_OK;
  if (!app->active || value.transfer_id != app->transfer_id)
    code = CONTROL_BULK_STATUS_INVALID;
  else if (!value.data.length || value.data.length > app->chunk_size ||
      value.offset > app->total || value.data.length > app->total - value.offset)
    code = CONTROL_BULK_STATUS_INVALID;
  else if (value.offset > app->next ||
      (value.offset < app->next && value.data.length > app->next - value.offset))
    code = CONTROL_BULK_STATUS_OUT_OF_ORDER;
  if (code == CONTROL_BULK_STATUS_OK) {
    for (size_t i = 0; i < value.data.length; ++i)
      if (value.data.data[i] != (uint8_t)((uint8_t)(value.offset + i) ^ UINT8_C(0xa5))) {
        code = CONTROL_BULK_STATUS_INTEGRITY_FAILED; break;
      }
    if (code == CONTROL_BULK_STATUS_OK) {
      if (value.offset == app->next) {
        app->crc = wl_crc32c_update(app->crc, value.data.data, value.data.length);
        app->next += value.data.length; ++app->writes;
      } else ++app->duplicates; /* This pattern sink can validate old bytes. */
      app->touched_at = app->now;
    }
  }
  return status(app, value.transfer_id, CONTROL_BULK_PHASE_CHUNK, code);
}
static wl_err_t end(usb_sample_application_t *app, const wl_kernel_frame_t *frame) {
  bulk_end_t value;
  if (bulk_end_decode(frame->payload, frame->length, &value) != WL_CODEC_OK ||
      !value.has_transfer_id || !value.has_total_length || !value.has_object_crc32c)
    return WL_ERR_CORRUPT_PAYLOAD;
  int code = CONTROL_BULK_STATUS_OK;
  if (!app->active && value.transfer_id == app->completed_id && value.transfer_id != 0) {
    if (value.total_length != app->completed_length || value.object_crc32c != app->completed_crc)
      code = CONTROL_BULK_STATUS_CONFLICT;
  } else if (!app->active || value.transfer_id != app->transfer_id)
    code = CONTROL_BULK_STATUS_INVALID;
  else if (value.total_length != app->total || app->next != app->total)
    code = CONTROL_BULK_STATUS_OUT_OF_ORDER;
  else if ((app->crc ^ UINT32_MAX) != app->expected_crc || value.object_crc32c != app->expected_crc)
    code = CONTROL_BULK_STATUS_INTEGRITY_FAILED;
  else {
    app->active = false; ++app->commits;
    app->completed_id = app->transfer_id; app->completed_length = app->total;
    app->completed_crc = app->expected_crc; app->completed_writes = app->writes;
    app->elapsed = app->now - app->started_at; app->report_due = app->now + 100;
    app->report_pending = true;
  }
  return status(app, value.transfer_id, CONTROL_BULK_PHASE_END, code);
}
wl_err_t usb_sample_route(wl_kernel_t *core, const wl_kernel_frame_t *frame) {
  usb_sample_application_t *app = core->user;
  /* A fair kernel may grant RX a turn before submitting the previous echo.
   * Retain that input without decoding or applying any business side effect. */
  if (app->output_pending) return WL_ERR_WOULD_BLOCK;
  switch (frame->message_id) {
    case BULK_BEGIN_MESSAGE_ID: return begin(app, frame);
    case BULK_CHUNK_MESSAGE_ID: return chunk(app, frame);
    case BULK_END_MESSAGE_ID: return end(app, frame);
    case BULK_ABORT_MESSAGE_ID: {
      bulk_abort_t value;
      if (bulk_abort_decode(frame->payload, frame->length, &value) != WL_CODEC_OK ||
          !value.has_transfer_id || !value.has_reason) return WL_ERR_CORRUPT_PAYLOAD;
      int code = CONTROL_BULK_STATUS_INVALID;
      if (app->active && value.transfer_id == app->transfer_id) {
        app->active = false; ++app->aborts; code = CONTROL_BULK_STATUS_ABORTED;
      }
      return status(app, value.transfer_id, CONTROL_BULK_PHASE_ABORT, code);
    }
    default:
      if (frame->length > sizeof(app->pending)) return WL_ERR_BUF_TOO_SMALL;
      memcpy(app->pending, frame->payload, frame->length); app->pending_length = frame->length;
      app->pending_id = frame->message_id; app->output_pending = true; return WL_OK;
  }
}
static wl_err_t encode(const void *user, uint8_t *out, size_t capacity, size_t *length) {
  const usb_sample_application_t *app = user;
  if (capacity < app->pending_length) return WL_ERR_BUF_TOO_SMALL;
  memcpy(out, app->pending, app->pending_length); *length = app->pending_length; return WL_OK;
}
static wl_err_t flush(usb_sample_application_t *app, wl_kernel_t *core) {
  if (!app->output_pending) return WL_OK;
  wl_err_t error = wl_kernel_send(core, app->pending_id, app, encode);
  if (error == WL_OK) {
    app->output_pending = false;
    /* One queued application output is a finite owner wake, never a physical
     * completion. A BUSY attempt consumes it and waits for real I/O activity. */
    wl_packet_activity(core->port);
  }
  return error == WL_ERR_WOULD_BLOCK ? WL_OK : error;
}
static int tx_progress(const usb_sample_application_t *app, const wl_kernel_t *core) {
  (void)app;
  return wl_packet_tx_ready(core->port) || (!wl_packet_tx_active(core->port) &&
    wl_packet_activity_pending(core->port));
}
wl_err_t usb_sample_pass(usb_sample_application_t *app, wl_kernel_t *core, uint32_t now) {
  app->now = now;
  if (!app->output_pending && app->active && now - app->touched_at >= 1000) {
    app->active = false; ++app->aborts;
    wl_err_t error = status(app, app->transfer_id, CONTROL_BULK_PHASE_ABORT, CONTROL_BULK_STATUS_TIMED_OUT);
    if (error != WL_OK) return error;
  }
  wl_err_t error = flush(app, core);
  if (error != WL_OK) return error;
  /* Only this raw echo/object tool has this application gate. A pending output
   * applies pressure in route, independently of the kernel's RX/TX preference.
   * Three events allow completion, a blocked RX turn and TX submission without
   * consuming the only wake before the queued output can progress. No RPC is
   * registered, so this gate cannot suppress a client deadline. */
  if (!app->output_pending || tx_progress(app, core)) {
    error = wl_kernel_step(core, now, 3U);
    if (error != WL_OK) return error;
  }
  return flush(app, core);
}
int usb_sample_ready(const usb_sample_application_t *app, const wl_kernel_t *core, uint32_t now) {
  if (app->output_pending) return tx_progress(app, core);
  int has_deadline = 0; uint32_t deadline = 0;
  return wl_kernel_ready(core, now, &has_deadline, &deadline);
}
uint32_t usb_sample_deadline(const usb_sample_application_t *app, uint32_t now) {
  if (!app->active || app->output_pending) return UINT32_MAX;
  uint32_t elapsed = now - app->touched_at;
  return elapsed >= 1000 ? 0 : 1000 - elapsed;
}
void usb_sample_close(usb_sample_application_t *app) {
  if (app->active) { app->active = false; ++app->aborts; }
  app->output_pending = false;
}

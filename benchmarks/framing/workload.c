/* SPDX-License-Identifier: Apache-2.0 */
#include "workload.h"
#include "wirelink/cobs.h"
#include <string.h>

const char *framing_mode_name(unsigned mode) {
  static const char *const names[] = {"spare", "exact", "overlap", "send", "claim", "busy3", "retry3"};
  return mode < FRAMING_MODES ? names[mode] : "invalid";
}

static wl_sink_result_t sink(void *context, wl_io_token_t token,
                             const uint8_t *data, size_t length) {
  framing_fixture_t *f = context;
  ++f->calls;
  f->token = token;
  f->last_data = data;
  f->last_len = length;
  if (f->busy != 0U) { --f->busy; return WL_SINK_BUSY; }
  return WL_SINK_SENT;
}

/* Independent COBS oracle: native frame + standalone COBS encoder. */
static int reference(framing_fixture_t *f) {
  size_t raw_len;
  int error = wl_frame_encode(&f->packet, WL_ENVELOPE_NATIVE_PACKET,
      f->raw, sizeof(f->raw), &raw_len);
  if (error != WL_OK) return error;
  if (f->envelope == WL_ENVELOPE_COBS_STREAM) {
    error = wl_cobs_encode(f->raw, raw_len, f->expected, sizeof(f->expected) - 1U, &f->exact_len);
    if (error != WL_OK) return error;
    f->expected[f->exact_len++] = 0U;
  } else if (f->envelope == WL_ENVELOPE_BUS_LENGTH16) {
    f->expected[0] = (uint8_t)(raw_len >> 8U);
    f->expected[1] = (uint8_t)raw_len;
    memcpy(f->expected + 2U, f->raw, raw_len);
    f->exact_len = raw_len + 2U;
  } else {
    memcpy(f->expected, f->raw, raw_len);
    f->exact_len = raw_len;
  }
  return WL_OK;
}

int framing_init(framing_fixture_t *f, unsigned mode, wl_envelope_type_t envelope,
                 wl_integrity_t integrity, unsigned pattern, size_t length) {
  if (f == NULL || mode >= FRAMING_MODES || pattern > 2U || length > WL_FRAME_MAX_PAYLOAD)
    return WL_ERR_INVALID_ARG;
  memset(f, 0, sizeof(*f));
  f->mode = mode;
  f->envelope = envelope;
  for (size_t i = 0U; i < length; ++i)
    f->payload[i] = pattern == 0U || (pattern == 2U && i % 17U == 0U)
        ? 0U : (uint8_t)(i % 251U + 1U);
  f->packet = (wl_wire_packet_t){.type = WL_PACKET_DATA, .integrity = integrity,
      .flags = mode == FRAMING_RETRY3 ? WL_PACKET_FLAG_RELIABLE : 0U,
      .message_id = 21U, .session_id = UINT64_C(0x123456789abcdef0),
      .payload = f->payload, .payload_len = length};
  wl_config_t config = {.max_payload_len = WL_FRAME_MAX_PAYLOAD, .envelope = envelope,
      .integrity = integrity, .session_id = f->packet.session_id,
      .max_retries = 3U, .ack_timeout_ms = 5U};
  wl_storage_t storage = {f->tx_payload, sizeof(f->tx_payload),
      f->tx_unit, sizeof(f->tx_unit), f->control, sizeof(f->control),
      f->fifo, sizeof(f->fifo), f->fallback, sizeof(f->fallback)};
  int error = wl_init(&f->link, &config, &storage);
  if (error == WL_OK) error = wl_set_sink(&f->link, sink, f);
  return error == WL_OK ? reference(f) : error;
}

int framing_step(framing_fixture_t *f) {
  wl_event_t event;
  int error;
  if (f->mode <= FRAMING_OVERLAP) {
    wl_wire_packet_t packet = f->packet;
    if (f->mode == FRAMING_OVERLAP) {
      memcpy(f->tx_unit, f->payload, f->packet.payload_len);
      packet.payload = f->tx_unit;
    }
    return wl_frame_encode(&packet, f->envelope, f->tx_unit,
        f->mode == FRAMING_EXACT ? f->exact_len : sizeof(f->tx_unit), &f->output_len);
  }
  f->now += 50U;
  if (f->mode == FRAMING_RETRY3) {
    wl_tx_handle_t handle;
    wl_tx_result_t result;
    f->packet.sequence = f->sequence++;
    error = wl_send_reliable(&f->link, f->packet.message_id, f->payload,
        f->packet.payload_len, f->now, &handle);
    if (error != WL_OK) return error;
    for (unsigned i = 1U; i <= 3U; ++i)
      if (wl_poll(&f->link, f->now + i * 5U, &event) != WL_ERR_NO_DATA) return WL_ERR_INVALID_STATE;
    if (wl_tx_cancel(&f->link, handle) != WL_OK || wl_tx_take(&f->link, handle, &result) != WL_OK ||
        result.retries_used != 3U) return WL_ERR_INVALID_STATE;
    return WL_OK;
  }
  if (f->mode == FRAMING_CLAIM) {
    wl_tx_payload_claim_t claim;
    error = wl_tx_payload_claim(&f->link, f->packet.message_id, WL_DELIVERY_UNRELIABLE, &claim);
    if (error != WL_OK) return error;
    memcpy(claim.span.data, f->payload, f->packet.payload_len);
    error = wl_tx_payload_commit(&f->link, &claim, f->packet.payload_len, f->now, NULL);
  } else {
    f->busy = f->mode == FRAMING_BUSY3 ? 3U : 0U;
    error = wl_send_unreliable(&f->link, f->packet.message_id, f->payload, f->packet.payload_len);
  }
  if (error != WL_OK) return error;
  while (f->busy != 0U)
    if (wl_poll(&f->link, f->now, &event) != WL_ERR_NO_DATA) return WL_ERR_INVALID_STATE;
  if (wl_poll(&f->link, f->now, &event) != WL_OK || event.type != WL_EVT_TX_SUCCESS)
    return WL_ERR_INVALID_STATE;
  return WL_OK;
}

int framing_check(framing_fixture_t *f) {
  int error = reference(f);
  if (error != WL_OK) return error;
  const size_t length = f->mode <= FRAMING_OVERLAP ? f->output_len : f->last_len;
  const uint8_t *data = f->mode <= FRAMING_OVERLAP ? f->tx_unit : f->last_data;
  return data != NULL && length == f->exact_len && memcmp(data, f->expected, length) == 0
      ? WL_OK : WL_ERR_CORRUPT_PAYLOAD;
}

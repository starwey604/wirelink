/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include "wirelink/crc.h"
#include "wirelink/cobs.h"
#include "wirelink/frame.h"

#include "crc_internal.h"
#include "frame_profile.h"
#include "frame_internal.h"

typedef struct {
  size_t encoded_len;
  unsigned int code;
} wl_cobs_count_state_t;

size_t wl_frame_packet_header_size(wl_packet_type_t type, uint8_t flags) {
  if (type == WL_PACKET_ACK && flags == 0U) {
    return WL_FRAME_RELIABLE_HEADER_SIZE;
  }
  if (type != WL_PACKET_DATA ||
      (flags & (uint8_t)~WL_PACKET_FLAG_RELIABLE) != 0U) {
    return 0U;
  }
  return (flags & WL_PACKET_FLAG_RELIABLE) != 0U
             ? WL_FRAME_RELIABLE_HEADER_SIZE
             : WL_FRAME_BASE_HEADER_SIZE;
}

static size_t frame_write_integrity(const wl_wire_packet_t *packet,
                                    wl_integrity_t integrity_kind,
                                    const uint8_t *header,
                                    size_t header_len,
                                    uint8_t integrity[WL_FRAME_MAX_CRC]) {
  (void)integrity_kind;
  if (integrity_kind == WL_INTEGRITY_CRC16)
  {
    uint16_t state = wl_crc16_ccitt_false_update(
        0xFFFFu, header, header_len);
    state = wl_crc16_ccitt_false_update(state, packet->payload,
                                        packet->payload_len);
    write_u16_be(integrity, state);
    return 2U;
  }

  if (integrity_kind == WL_INTEGRITY_CRC32C) {
    uint32_t state =
        wl_crc32c_update(0xFFFFFFFFu, header, header_len);
    state = wl_crc32c_update(state, packet->payload, packet->payload_len);
    write_u32_be(integrity, state ^ 0xFFFFFFFFu);
    return 4U;
  }

  return 0U;
}

static void cobs_count_span(wl_cobs_count_state_t *state, const uint8_t *data,
                            size_t length) {
  /* Each input byte contributes one output byte, including a replaced zero.
   * Only a full nonzero run contributes an extra code byte. */
  size_t encoded_len = state->encoded_len + length;
  unsigned int code = state->code;
  for (size_t i = 0U; i < length; ++i) {
    code = data[i] == 0U ? 1U : code + 1U;
    if (code == 0xFFU) {
      ++encoded_len;
      code = 1U;
    }
  }
  state->encoded_len = encoded_len;
  state->code = code;
}

static size_t frame_cobs_encoded_size(const uint8_t *header,
                                      size_t header_len,
                                      const uint8_t *payload,
                                      size_t payload_len,
                                      const uint8_t *integrity,
                                      size_t integrity_len) {
  wl_cobs_count_state_t state = {.encoded_len = 1U, .code = 1U};

  cobs_count_span(&state, header, header_len);
  cobs_count_span(&state, payload, payload_len);
  cobs_count_span(&state, integrity, integrity_len);
  return state.encoded_len;
}

static void cobs_encode_span(wl_cobs_encode_state_t *state,
                             const uint8_t *data, size_t length) {
  if (length == 0U) {
    return;
  }
  /* State is private stack storage, separate from every input/output span.
   * Publish once per span instead of reloading it after every byte store.
   * The caller stages aliased payload ahead of output; read each byte before
   * writing it to preserve that forward-encoding invariant. */
  uint8_t *const out = state->out;
  size_t write_index = state->write_index;
  size_t code_index = state->code_index;
  uint8_t code = state->code;
  for (size_t i = 0U; i < length; ++i) {
    const uint8_t value = data[i];
    cobs_encode_byte(out, &write_index, &code_index, &code, value);
  }
  state->write_index = write_index;
  state->code_index = code_index;
  state->code = code;
}

static size_t frame_cobs_encode(const uint8_t *header,
                                size_t header_len,
                                const uint8_t *payload, size_t payload_len,
                                const uint8_t *integrity,
                                size_t integrity_len, uint8_t *out) {
  wl_cobs_encode_state_t state = {
      .out = out, .write_index = 1U, .code_index = 0U, .code = 1U};

  cobs_encode_span(&state, header, header_len);
  cobs_encode_span(&state, payload, payload_len);
  cobs_encode_span(&state, integrity, integrity_len);
  state.out[state.code_index] = state.code;
  return state.write_index;
}

static inline void frame_write_raw(const uint8_t *header,
                            size_t header_len,
                            const wl_wire_packet_t *packet,
                            const uint8_t *integrity, size_t integrity_len,
                            uint8_t *out) {
  /* Moving the payload first also preserves supported overlapping inputs. */
  if (packet->payload_len != 0U) {
    memmove(out + header_len, packet->payload, packet->payload_len);
  }
  /* Both validated header variants share the four-byte base. Fixed-size
   * copies let the compiler inline these small fields on packet envelopes. */
  memcpy(out, header, WL_FRAME_BASE_HEADER_SIZE);
  if (header_len == WL_FRAME_RELIABLE_HEADER_SIZE) {
    memcpy(out + WL_FRAME_BASE_HEADER_SIZE, header + WL_FRAME_BASE_HEADER_SIZE,
           WL_FRAME_RELIABLE_HEADER_SIZE - WL_FRAME_BASE_HEADER_SIZE);
  }
  if (integrity_len != 0U) {
    memcpy(out + header_len + packet->payload_len, integrity,
           integrity_len);
  }
}

size_t wl_frame_overhead(wl_integrity_t integrity) {
  if (!wl_profile_integrity_valid(integrity) ||
      !wl_profile_supported(WL_ENVELOPE_COBS_STREAM, integrity)) {
    return 0U;
  }
  return (size_t)WL_FRAME_HEADER_SIZE + wl_profile_crc_size(integrity);
}

size_t wl_frame_encode_overhead(wl_envelope_type_t envelope,
                               wl_integrity_t integrity) {
  size_t raw_size;

  if (!wl_profile_envelope_valid(envelope) ||
      !wl_profile_integrity_valid(integrity) ||
      !wl_profile_supported(envelope, integrity)) {
    return 0U;
  }
  raw_size = wl_frame_raw_size(0, integrity);
  if (raw_size == 0) {
    return 0;
  }

  switch (envelope) {
  case WL_ENVELOPE_COBS_STREAM:
    return wl_cobs_encoded_max_size(raw_size) + 1;
  case WL_ENVELOPE_BUS_LENGTH16:
    return raw_size + 2;
  case WL_ENVELOPE_NATIVE_PACKET:
    return raw_size;
  default:
    return 0U;
  }
}

size_t wl_frame_raw_size(size_t payload_len, wl_integrity_t integrity) {
  size_t crc_len;

  if (!wl_profile_integrity_valid(integrity) ||
      !wl_profile_supported(WL_ENVELOPE_COBS_STREAM, integrity)) {
    return 0U;
  }
  crc_len = wl_profile_crc_size(integrity);
  if (payload_len > (SIZE_MAX - WL_FRAME_HEADER_SIZE - crc_len)) {
    return 0;
  }
  return WL_FRAME_HEADER_SIZE + payload_len + crc_len;
}

wl_err_t wl_frame_validate_header(const wl_frame_header_t *hdr) {
  if (hdr == NULL) {
    return WL_ERR_INVALID_ARG;
  }

  if ((hdr->marker_version_kind & WL_FRAME_MARKER_MASK) != WL_FRAME_MARKER ||
      hdr->reserved != 0U) {
    return WL_ERR_BAD_FRAME;
  }
  if (hdr->version != WL_FRAME_VERSION) {
    return WL_ERR_PROTOCOL_VERSION;
  }
  if (hdr->payload_length > WL_PACKET_MAX_PAYLOAD) {
    return WL_ERR_FRAME_TOO_LONG;
  }

  switch (hdr->packet_type) {
  case WL_PACKET_DATA:
    if (hdr->message_id == 0U) {
      return WL_ERR_BAD_FRAME;
    }
    if (hdr->flags & ~WL_PACKET_FLAG_RELIABLE) {
      return WL_ERR_BAD_FRAME;
    }
    if ((hdr->flags & WL_PACKET_FLAG_RELIABLE) != 0U) {
      if (hdr->header_length != WL_FRAME_RELIABLE_HEADER_SIZE ||
          hdr->session_id == 0ULL) {
        return WL_ERR_BAD_FRAME;
      }
    } else if (hdr->header_length != WL_FRAME_BASE_HEADER_SIZE ||
               hdr->session_id != 0ULL || hdr->sequence != 0U) {
      return WL_ERR_BAD_FRAME;
    }
    break;
  case WL_PACKET_ACK:
    if (hdr->message_id != 0U || hdr->payload_length != 0U || hdr->flags != 0U ||
        hdr->header_length != WL_FRAME_RELIABLE_HEADER_SIZE ||
        hdr->session_id == 0ULL) {
      return WL_ERR_BAD_FRAME;
    }
    break;
  case WL_PACKET_NACK:
    return WL_ERR_NOT_SUPPORTED;
  default:
    return WL_ERR_NOT_SUPPORTED;
  }

  return WL_OK;
}

static int frame_header_size(const wl_wire_packet_t *packet) {
  if (packet->type == WL_PACKET_DATA) {
    if (packet->message_id == 0U) {
      return WL_ERR_INVALID_ARG;
    }
    if (packet->flags & ~WL_PACKET_FLAG_RELIABLE) {
      return WL_ERR_INVALID_ARG;
    }
    if ((packet->flags & WL_PACKET_FLAG_RELIABLE) != 0U &&
        packet->session_id == 0ULL) {
      return WL_ERR_INVALID_ARG;
    }
  } else if (packet->type == WL_PACKET_ACK) {
    if (packet->message_id != 0U || packet->payload_len != 0U ||
        packet->flags != 0U) {
      return WL_ERR_INVALID_ARG;
    }
    if (packet->session_id == 0ULL) {
      return WL_ERR_INVALID_ARG;
    }
  } else {
    return WL_ERR_BAD_FRAME;
  }
  if (packet->payload_len > WL_PACKET_MAX_PAYLOAD) {
    return WL_ERR_PAYLOAD_TOO_LONG;
  }
  return (int)wl_frame_packet_header_size(packet->type, packet->flags);
}
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
int wl_frame_encode_bounded(const wl_wire_packet_t *packet,
                                wl_envelope_type_t envelope, wl_integrity_t integrity_kind,
                                const uint8_t *header, size_t header_len,
                                size_t raw_len, uint8_t *out, size_t out_cap,
                                size_t *out_len);
static int frame_encode_generic(const wl_wire_packet_t *packet, wl_envelope_type_t envelope,
                            uint8_t *out, size_t out_cap, size_t *out_len) {
  uint8_t header[WL_FRAME_HEADER_SIZE];
  const size_t header_len = packet->type == WL_PACKET_ACK || packet->flags != 0U
      ? WL_FRAME_RELIABLE_HEADER_SIZE : WL_FRAME_BASE_HEADER_SIZE;
  const size_t raw_len = header_len + packet->payload_len + wl_profile_crc_size(packet->integrity);
  *out_len = 0U;
  frame_write_header(packet, header_len, header);
  return wl_frame_encode_bounded(packet, envelope, packet->integrity,
                                 header, header_len, raw_len, out, out_cap, out_len);
}

int wl_frame_decode(const uint8_t *in, size_t in_len, wl_integrity_t integrity,
                   wl_frame_view_t *out_view) {
  if (out_view == NULL || in == NULL || !wl_profile_integrity_valid(integrity))
    return WL_ERR_INVALID_ARG;
  if (!wl_profile_supported(WL_ENVELOPE_COBS_STREAM, integrity)) return WL_ERR_NOT_SUPPORTED;
  return frame_parse(in, in_len, integrity, wl_profile_crc_size(integrity), NULL, out_view);
}

/* Standalone tools support the frozen framing formats. The selected endpoint
 * transports do not dispatch through this generic utility. */
int wl_frame_encode(const wl_wire_packet_t *packet, wl_envelope_type_t envelope,
                   uint8_t *out, size_t out_cap, size_t *out_len) {
  if (out_len == NULL) return WL_ERR_INVALID_ARG;
  *out_len = 0U;
  if (packet == NULL || (packet->payload_len != 0U && packet->payload == NULL) ||
      !wl_profile_envelope_valid(envelope) ||
      !wl_profile_integrity_valid(packet->integrity)) return WL_ERR_INVALID_ARG;
  const int header_len = frame_header_size(packet);
  if (header_len < 0) return header_len;
  if (!wl_profile_supported(envelope, packet->integrity)) return WL_ERR_NOT_SUPPORTED;
  if (envelope == WL_ENVELOPE_COBS_STREAM && packet->integrity == WL_INTEGRITY_CRC16)
    return wl_frame_encode_cobs_crc16(packet, out, out_cap, out_len);
  return frame_encode_generic(packet, envelope, out, out_cap, out_len);
}

int wl_frame_encode_bounded(const wl_wire_packet_t *packet,
                                wl_envelope_type_t envelope, wl_integrity_t integrity_kind,
                                const uint8_t *header, size_t header_len,
                                size_t raw_len, uint8_t *out, size_t out_cap,
                                size_t *out_len) {
  uint8_t integrity[WL_FRAME_MAX_CRC];
  (void)envelope;
  (void)integrity_kind;
  size_t crc_len = frame_write_integrity(packet, integrity_kind, header, header_len, integrity);

  switch (envelope) {
  case WL_ENVELOPE_COBS_STREAM:
  {
    const uint8_t *payload = packet->payload;
    size_t cobs_len = wl_cobs_encoded_max_size(raw_len);
    const int overlap = spans_overlap(payload, packet->payload_len, out, cobs_len);
    /* Normal link storage fits the worst case. Disjoint input can therefore
     * be encoded in one pass. Tight buffers and aliases still need the exact
     * size: preserve failure-before-write and the overlap staging offset. */
    if (out_cap < cobs_len + 1U || overlap) {
      cobs_len = frame_cobs_encoded_size(
          header, header_len, payload, packet->payload_len, integrity, crc_len);
    }
    if (out_cap < cobs_len + 1U) {
      return WL_ERR_BUF_TOO_SMALL;
    }
    if (out == NULL) {
      return WL_ERR_INVALID_ARG;
    }
    if (overlap) {
      /*
       * COBS expands by at least one byte. Staging the aliased payload at
       * its logical raw-frame offset leaves enough lead for safe forward
       * encoding, while keeping the normal non-aliased path copy-free. A
       * worst-case-only overlap may stage unnecessarily, but all staging
       * writes fit the exact output size and need no second overlap check.
       */
      payload = out + (cobs_len - raw_len) + header_len;
      memmove((uint8_t *)payload, packet->payload, packet->payload_len);
    }
    cobs_len = frame_cobs_encode(header, header_len, payload, packet->payload_len,
                                 integrity, crc_len, out);
    out[cobs_len] = 0U;
    *out_len = cobs_len + 1U;
    return WL_OK;
  }
  case WL_ENVELOPE_BUS_LENGTH16:
    if (raw_len > UINT16_MAX) {
      return WL_ERR_FRAME_TOO_LONG;
    }
    if (out_cap < raw_len + 2U) {
      return WL_ERR_BUF_TOO_SMALL;
    }
    if (out == NULL) {
      return WL_ERR_INVALID_ARG;
    }
    frame_write_raw(header, header_len, packet, integrity, crc_len, out + 2U);
    write_u16_be(out, (uint16_t)raw_len);
    *out_len = raw_len + 2U;
    return WL_OK;
  case WL_ENVELOPE_NATIVE_PACKET:
  default:
    if (out_cap < raw_len) {
      return WL_ERR_BUF_TOO_SMALL;
    }
    if (out == NULL) {
      return WL_ERR_INVALID_ARG;
    }
    frame_write_raw(header, header_len, packet, integrity, crc_len, out);
    *out_len = raw_len;
    return WL_OK;
  }
}

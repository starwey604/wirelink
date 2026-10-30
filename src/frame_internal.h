/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_SRC_FRAME_INTERNAL_H_
#define WIRELINK_SRC_FRAME_INTERNAL_H_
#include "wirelink/frame.h"
#include "wirelink/link.h"
#include "wirelink/crc.h"
#include "crc_internal.h"
/* Two entry points share these kernels: public checked tools and owner-local
 * link framing. Inline at that boundary so each keeps only its own validation
 * and integrity work under -Os, without interprocedural/LTO build changes. */
#if defined(__GNUC__) || defined(__clang__)
#define WL_FRAME_INLINE static inline __attribute__((always_inline))
#else
#define WL_FRAME_INLINE static inline
#endif

WL_FRAME_INLINE uint16_t read_u16_be(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] << 8u) | (uint16_t)p[1];
}

WL_FRAME_INLINE uint32_t read_u32_be(const uint8_t *p) {
  return ((uint32_t)p[0] << 24u) | ((uint32_t)p[1] << 16u) |
         ((uint32_t)p[2] << 8u) | (uint32_t)p[3];
}

WL_FRAME_INLINE uint64_t read_u64_be(const uint8_t *p) {
  return ((uint64_t)p[0] << 56u) | ((uint64_t)p[1] << 48u) |
         ((uint64_t)p[2] << 40u) | ((uint64_t)p[3] << 32u) |
         ((uint64_t)p[4] << 24u) | ((uint64_t)p[5] << 16u) |
         ((uint64_t)p[6] << 8u) | (uint64_t)p[7];
}

WL_FRAME_INLINE void write_u16_be(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v >> 8u);
  p[1] = (uint8_t)(v & 0xFFu);
}

WL_FRAME_INLINE void write_u32_be(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24u);
  p[1] = (uint8_t)(v >> 16u);
  p[2] = (uint8_t)(v >> 8u);
  p[3] = (uint8_t)(v & 0xFFu);
}

WL_FRAME_INLINE void write_u64_be(uint8_t *p, uint64_t v) {
  p[0] = (uint8_t)(v >> 56u);
  p[1] = (uint8_t)(v >> 48u);
  p[2] = (uint8_t)(v >> 40u);
  p[3] = (uint8_t)(v >> 32u);
  p[4] = (uint8_t)(v >> 24u);
  p[5] = (uint8_t)(v >> 16u);
  p[6] = (uint8_t)(v >> 8u);
  p[7] = (uint8_t)(v & 0xFFu);
}

typedef struct {
  uint8_t *out;
  size_t write_index;
  size_t code_index;
  uint8_t code;
} wl_cobs_encode_state_t;
WL_FRAME_INLINE uint8_t frame_packet_kind(const wl_wire_packet_t *packet) {
  if (packet->type == WL_PACKET_ACK) {
    return 2U;
  }
  return (packet->flags & WL_PACKET_FLAG_RELIABLE) != 0U ? 1U : 0U;
}
WL_FRAME_INLINE void frame_write_header(const wl_wire_packet_t *packet, size_t header_len,
                                uint8_t header[WL_FRAME_HEADER_SIZE]) {
  header[0] = (uint8_t)(WL_FRAME_PREFIX | frame_packet_kind(packet));
  header[1] = 0U;
  write_u16_be(&header[2], packet->message_id);
  if (header_len == WL_FRAME_RELIABLE_HEADER_SIZE) {
    write_u64_be(&header[4], (uint64_t)packet->session_id);
    write_u32_be(&header[12], packet->sequence);
  }
}
WL_FRAME_INLINE void cobs_encode_byte(uint8_t *out, size_t *write_index,
                                    size_t *code_index, uint8_t *code,
                                    uint8_t value) {
  if (value == 0U) {
    out[*code_index] = *code;
    *code = 1U;
    *code_index = (*write_index)++;
  } else {
    out[(*write_index)++] = value;
    ++*code;
    if (*code == 0xffU) {
      out[*code_index] = *code;
      *code = 1U;
      *code_index = (*write_index)++;
    }
  }
}
WL_FRAME_INLINE int spans_overlap(const uint8_t *left, size_t left_len,
                         const uint8_t *right, size_t right_len) {
  uintptr_t left_address;
  uintptr_t right_address;

  if (left_len == 0U || right_len == 0U) {
    return 0;
  }

  left_address = (uintptr_t)left;
  right_address = (uintptr_t)right;
  if (left_address <= right_address) {
    return (right_address - left_address) < left_len;
  }
  return (left_address - right_address) < right_len;
}
WL_FRAME_INLINE int frame_parse(const uint8_t *in, size_t in_len, wl_integrity_t integrity, size_t crc_len,
                       const uint16_t *cobs_crc, wl_frame_view_t *out_view) {
  size_t header_len;
  size_t payload_len;
  wl_packet_type_t packet_type;
  uint16_t message_id;
  uint64_t session_id = 0U;
  uint32_t sequence = 0U;
  uint8_t flags;
  uint8_t kind;

  if (out_view == NULL || in == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (in_len < WL_FRAME_BASE_HEADER_SIZE + crc_len) {
    return WL_ERR_BAD_FRAME;
  }

  if ((in[0] & WL_FRAME_MARKER_MASK) != WL_FRAME_MARKER) {
    return WL_ERR_BAD_FRAME;
  }
  if (((in[0] & WL_FRAME_VERSION_MASK) >> WL_FRAME_VERSION_SHIFT) !=
      WL_FRAME_VERSION) {
    return WL_ERR_PROTOCOL_VERSION;
  }
  kind = in[0] & WL_FRAME_KIND_MASK;
  switch (kind) {
  case 0U:
    packet_type = WL_PACKET_DATA;
    flags = 0U;
    header_len = WL_FRAME_BASE_HEADER_SIZE;
    break;
  case 1U:
    packet_type = WL_PACKET_DATA;
    flags = WL_PACKET_FLAG_RELIABLE;
    header_len = WL_FRAME_RELIABLE_HEADER_SIZE;
    break;
  case 2U:
    packet_type = WL_PACKET_ACK;
    flags = 0U;
    header_len = WL_FRAME_RELIABLE_HEADER_SIZE;
    break;
  default:
    return WL_ERR_NOT_SUPPORTED;
  }
  if (in_len < header_len + crc_len) {
    return WL_ERR_BAD_FRAME;
  }
  payload_len = in_len - header_len - crc_len;
  if (payload_len > WL_PACKET_MAX_PAYLOAD) {
    return WL_ERR_FRAME_TOO_LONG;
  }
  message_id = read_u16_be(in + 2U);
  if (header_len == WL_FRAME_RELIABLE_HEADER_SIZE) {
    session_id = read_u64_be(in + 4U);
    sequence = read_u32_be(in + 12U);
  }

  /* Kind determines flags and header length, and the checks above establish
   * marker, version and payload bounds. Validate the remaining wire fields
   * once, preserving their precedence over integrity errors. */
  if (in[1] != 0U) {
    return WL_ERR_BAD_FRAME;
  }
  if (packet_type == WL_PACKET_DATA) {
    if (message_id == 0U ||
        (flags != 0U && session_id == 0U)) {
      return WL_ERR_BAD_FRAME;
    }
  } else if (message_id != 0U || payload_len != 0U || session_id == 0U) {
    return WL_ERR_BAD_FRAME;
  }

  const size_t covered = in_len - crc_len;
  if (integrity == WL_INTEGRITY_CRC16)
  {
    if (cobs_crc != NULL ? *cobs_crc != 0U :
        read_u16_be(in + covered) != wl_crc16_ccitt_false(in, covered))
      return WL_ERR_CRC;
  }
  else if (integrity == WL_INTEGRITY_CRC32C) {
    if (read_u32_be(in + covered) != wl_crc32c(in, covered)) return WL_ERR_CRC;
  }

  out_view->type = packet_type;
  out_view->flags = flags;
  out_view->message_id = message_id;
  out_view->session_id = session_id;
  out_view->sequence = sequence;
  out_view->integrity = integrity;
  out_view->payload.data = (uint8_t *)(in + header_len);
  out_view->payload.length = payload_len;
  out_view->integrity_bytes.data =
      (crc_len != 0U) ? (uint8_t *)(in + header_len + payload_len)
                     : NULL;
  out_view->integrity_bytes.length = crc_len;
  out_view->total_len = in_len;

  return WL_OK;
}
/* Both builds use this same complete, checked COBS + CRC16 implementation.
 * Encoding callers supply a validated packet; decoding publishes only after
 * COBS, structure and residue validation. No public skip-check option exists. */
int wl_frame_encode_cobs_crc16(const wl_config_t *config, const wl_wire_packet_t *packet,
                              uint8_t *out, size_t out_cap, size_t *out_len);
int wl_frame_decode_cobs_crc16(const wl_config_t *config, uint8_t *encoded, size_t encoded_len,
                              wl_frame_view_t *out_view);
/* Shared cold writer for aliases, exact buffers and other available modes. */
int wl_frame_encode_bounded(const wl_wire_packet_t *packet,
                           wl_envelope_type_t envelope, wl_integrity_t integrity,
                           const uint8_t *header, size_t header_len, size_t raw_len,
                           uint8_t *out, size_t out_cap, size_t *out_len);
void wl_frame_process_stream(wl_ctx_t *ctx);
void wl_frame_process_units(wl_ctx_t *ctx);
int wl_frame_receive_unit(wl_ctx_t *ctx, const uint8_t *unit, size_t length);
/* Owner-private: callers supply only a fully verified frame view. */
int wl_frame_route_view(wl_ctx_t *ctx, const wl_frame_view_t *view);
int wl_frame_bad_frame(wl_ctx_t *ctx, int error);
int wl_frame_decode_cobs_generic(const wl_config_t *config, uint8_t *encoded,
                                 size_t length, wl_frame_view_t *view);
int wl_frame_encode_generic(const wl_config_t *config, const wl_wire_packet_t *packet,
                            uint8_t *out, size_t out_cap, size_t *out_len);
#endif

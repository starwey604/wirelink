#ifndef INCLUDE_WIRELINK_FRAME_H_
#define INCLUDE_WIRELINK_FRAME_H_

#include <stddef.h>
#include <stdint.h>

#include "wirelink/span.h"
#include "wirelink/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WL_MAGIC0 0x57U
#define WL_MAGIC1 0x4CU
#define WL_FRAME_VERSION 1
#define WL_FRAME_HEADER_SIZE 22U
#define WL_FRAME_MAX_PAYLOAD 2048U
#define WL_FRAME_MAX_CRC 4U
#define WL_FRAME_MAX_RAW_LEN (WL_FRAME_HEADER_SIZE + WL_FRAME_MAX_PAYLOAD + WL_FRAME_MAX_CRC)

/*
 * max COBS expansion = raw + raw/254 + 1 delimiter
 * for 2054 => 2074 + 9 + 1 = 2084
 */
#define WL_FRAME_MAX_COBS_LEN 2084U

typedef enum {
  WL_ENVELOPE_COBS_STREAM = 0,
  WL_ENVELOPE_NATIVE_PACKET = 1,
  WL_ENVELOPE_BUS_LENGTH16 = 2,
} wl_envelope_type_t;

typedef enum {
  WL_INTEGRITY_NONE = 0,
  WL_INTEGRITY_CRC16 = 1,
  WL_INTEGRITY_CRC32C = 2,
  WL_INTEGRITY_CRC32 = WL_INTEGRITY_CRC32C,
} wl_integrity_t;

typedef enum {
  WL_PACKET_DATA = 0x01,
  WL_PACKET_ACK = 0x02,
  WL_PACKET_NACK = 0x03,
  WL_PACKET_PRIVATE_USE = 0x80,
} wl_packet_type_t;

typedef enum {
  /* V1: only bit0 is defined as WL_PACKET_FLAG_RELIABLE. */
  WL_PACKET_FLAG_RELIABLE = 0x01,
  /* Used to reject illegal non-zero reserved bits deterministically. */
  WL_PACKET_FLAG_RESERVED_MASK = 0xFEU,
} wl_packet_flag_t;

typedef struct {
  uint8_t magic[2];
  uint8_t version;
  uint8_t header_length;
  uint8_t packet_type;
  uint8_t flags;
  uint64_t session_id;
  uint32_t sequence;
  uint16_t cmd_id;
  uint16_t payload_length;
} wl_frame_header_t;

typedef struct {
  wl_packet_type_t type;
  wl_integrity_t integrity;
  uint8_t flags;
  uint16_t cmd_id;
  uint64_t session_id;
  uint32_t sequence;
  const uint8_t *payload;
  size_t payload_len;
} wl_wire_packet_t;

typedef struct {
  wl_packet_type_t type;
  uint8_t flags;
  uint16_t cmd_id;
  uint64_t session_id;
  uint32_t sequence;
  wl_integrity_t integrity;
  wl_span_t payload;
  wl_span_t integrity_bytes;
  size_t total_len;
} wl_frame_view_t;

/* Internal hard limit used by the current parser/encoder implementation. */
#define WL_PACKET_MAX_PAYLOAD WL_FRAME_MAX_PAYLOAD

size_t wl_frame_overhead(wl_integrity_t integrity);
size_t wl_frame_encode_overhead(wl_envelope_type_t envelope, wl_integrity_t integrity);
size_t wl_frame_raw_size(size_t payload_len, wl_integrity_t integrity);

int wl_frame_encode(const wl_wire_packet_t *packet, wl_envelope_type_t envelope,
                   uint8_t *out, size_t out_cap, size_t *out_len);
int wl_frame_decode(const uint8_t *in, size_t in_len, wl_integrity_t integrity,
                   wl_frame_view_t *out_view);
wl_err_t wl_frame_validate_header(const wl_frame_header_t *hdr);

#ifdef __cplusplus
}
#endif

#endif // INCLUDE_WIRELINK_FRAME_H_

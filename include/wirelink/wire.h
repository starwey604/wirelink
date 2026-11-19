/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_WIRE_H_
#define WIRELINK_WIRE_H_

#include <stddef.h>
#include <stdint.h>
#include "wirelink/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WL_WIRE_NATIVE_HEADER 4U
#define WL_WIRE_MAGIC 0x57U
#define WL_WIRE_MAX_PAYLOAD 2048U
#define WL_WIRE_RPC_HEADER_DATA 8U
#define WL_WIRE_RPC_HEADER_ERR 12U
#define WL_WIRE_RPC_HEADER_MAX 12U

/* Stable C ABI even when a consumer uses -fshort-enums. */
enum {
  WL_WIRE_MSG = 0U,
  WL_WIRE_RPC_REQ = 1U,
  WL_WIRE_RPC_RESP = 2U,
  WL_WIRE_RPC_ERR = 3U
};

typedef struct {
  uint16_t message_id;
  uint8_t kind;
  const uint8_t *payload;
  size_t length;
} wl_wire_frame_t;

typedef struct {
  uint32_t session;
  uint32_t operation;
  int32_t status;
  const uint8_t *body;
  size_t length;
} wl_rpc_metadata_t;

static inline uint32_t wl_wire_get_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
         ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static inline void wl_wire_put_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8U);
  p[2] = (uint8_t)(v >> 16U);
  p[3] = (uint8_t)(v >> 24U);
}

/* Wire v2 4-byte little-endian frame header: [0x57, kind, le_route_id]. */
static inline wl_err_t wl_wire_frame_seal(uint8_t *frame, size_t capacity,
                                          uint8_t kind, uint16_t route_id,
                                          size_t payload_length) {
  if (frame == NULL || route_id == 0U || kind > WL_WIRE_RPC_ERR)
    return WL_ERR_INVALID_ARG;
  if (payload_length > WL_WIRE_MAX_PAYLOAD) return WL_ERR_PAYLOAD_TOO_LONG;
  if (capacity < WL_WIRE_NATIVE_HEADER + payload_length)
    return WL_ERR_BUF_TOO_SMALL;
  frame[0] = WL_WIRE_MAGIC;
  frame[1] = kind;
  frame[2] = (uint8_t)route_id;
  frame[3] = (uint8_t)(route_id >> 8U);
  return WL_OK;
}

static inline wl_err_t wl_wire_frame_read(const uint8_t *frame, size_t length,
                                          wl_wire_frame_t *out) {
  if (frame == NULL || out == NULL) return WL_ERR_INVALID_ARG;
  if (length < WL_WIRE_NATIVE_HEADER) return WL_ERR_BAD_FRAME;
  if (frame[0] != WL_WIRE_MAGIC) return WL_ERR_BAD_FRAME;
  if (frame[1] > WL_WIRE_RPC_ERR) return WL_ERR_NOT_SUPPORTED;
  size_t payload_len = length - WL_WIRE_NATIVE_HEADER;
  if (payload_len > WL_WIRE_MAX_PAYLOAD) return WL_ERR_FRAME_TOO_LONG;
  uint16_t route_id = (uint16_t)frame[2] | ((uint16_t)frame[3] << 8U);
  if (route_id == 0U) return WL_ERR_BAD_FRAME;
  out->message_id = route_id;
  out->kind = frame[1];
  out->payload = frame + WL_WIRE_NATIVE_HEADER;
  out->length = payload_len;
  return WL_OK;
}

static inline wl_err_t wl_wire_native_none_seal(uint8_t *frame, size_t capacity,
                                                  uint16_t message_id,
                                                  size_t payload_length) {
  return wl_wire_frame_seal(frame, capacity, WL_WIRE_MSG, message_id, payload_length);
}

static inline wl_err_t wl_wire_native_none_read(const uint8_t *frame, size_t length,
                                                  wl_wire_frame_t *out) {
  return wl_wire_frame_read(frame, length, out);
}

static inline size_t wl_wire_rpc_header_size(int32_t status) {
  return status == 0 ? WL_WIRE_RPC_HEADER_DATA : WL_WIRE_RPC_HEADER_ERR;
}

/* Wire v2 fixed little-endian RPC subheader: 8B req/resp or 12B err. */
static inline wl_err_t wl_wire_rpc_write(uint8_t *data, size_t capacity,
                                         uint32_t session, uint32_t operation,
                                         int32_t status, size_t *length) {
  if (length == NULL) return WL_ERR_INVALID_ARG;
  *length = 0U;
  if (data == NULL || session == 0U || operation == 0U) return WL_ERR_INVALID_ARG;
  size_t size = wl_wire_rpc_header_size(status);
  if (capacity < size) return WL_ERR_BUF_TOO_SMALL;
  wl_wire_put_u32(data, session);
  wl_wire_put_u32(data + 4U, operation);
  if (status != 0) wl_wire_put_u32(data + 8U, (uint32_t)status);
  *length = size;
  return WL_OK;
}

static inline wl_err_t wl_wire_rpc_read(uint8_t kind, const uint8_t *data,
                                        size_t length,
                                        wl_rpc_metadata_t *out) {
  if (data == NULL || out == NULL) return WL_ERR_INVALID_ARG;
  size_t hdr = kind == WL_WIRE_RPC_ERR ? WL_WIRE_RPC_HEADER_ERR
                                       : WL_WIRE_RPC_HEADER_DATA;
  if ((kind != WL_WIRE_RPC_REQ && kind != WL_WIRE_RPC_RESP &&
       kind != WL_WIRE_RPC_ERR) || length < hdr)
    return WL_ERR_CORRUPT_PAYLOAD;
  if (kind == WL_WIRE_RPC_ERR && length != hdr) return WL_ERR_CORRUPT_PAYLOAD;
  uint32_t session = wl_wire_get_u32(data);
  uint32_t operation = wl_wire_get_u32(data + 4U);
  int32_t status = 0;
  if (kind == WL_WIRE_RPC_ERR) {
    uint32_t bits = wl_wire_get_u32(data + 8U);
    status = bits <= INT32_MAX ? (int32_t)bits : -1 - (int32_t)(UINT32_MAX - bits);
    if (status == 0) return WL_ERR_CORRUPT_PAYLOAD;
  }
  if (session == 0U || operation == 0U) return WL_ERR_CORRUPT_PAYLOAD;
  out->session = session;
  out->operation = operation;
  out->status = status;
  out->body = data + hdr;
  out->length = length - hdr;
  return WL_OK;
}

#ifdef __cplusplus
}
#endif

#endif
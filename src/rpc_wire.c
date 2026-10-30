/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/kernel_wire.h"

#include <limits.h>

/* Keep the managed wire algorithm from wlc/src/managed_rpc_helpers.c.in.
 * These are metadata primitives, with no pending/replay/server execution. */
static uint32_t read_u32(const uint8_t *data) {
  return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
         ((uint32_t)data[2] << 8U) | data[3];
}

static void write_u32(uint8_t *data, uint32_t value) {
  data[0] = (uint8_t)(value >> 24U);
  data[1] = (uint8_t)(value >> 16U);
  data[2] = (uint8_t)(value >> 8U);
  data[3] = (uint8_t)value;
}

size_t wl_kernel_rpc_header_size(uint32_t operation, int32_t status) {
  if (operation == 0U) return 0U;
  size_t size = status == 0 ? 9U : 13U;
  operation >>= 6U;
  while (operation != 0U) { ++size; operation >>= 7U; }
  return size;
}

wl_err_t wl_kernel_rpc_write(uint8_t *data, size_t capacity, uint64_t session,
                             uint32_t operation, int32_t status, size_t *length) {
  if (length == NULL) return WL_ERR_INVALID_ARG;
  *length = 0U;
  if (data == NULL || session == 0U || operation == 0U) return WL_ERR_INVALID_ARG;
  size_t size = wl_kernel_rpc_header_size(operation, status);
  if (capacity < size) return WL_ERR_BUF_TOO_SMALL;
  write_u32(data, (uint32_t)(session >> 32U));
  write_u32(data + 4U, (uint32_t)session);
  data[8] = (uint8_t)(((operation & 63U) << 1U) | (status != 0 ? 1U : 0U));
  operation >>= 6U;
  if (operation != 0U) data[8] |= 128U;
  size_t at = 9U;
  while (operation != 0U) {
    uint8_t byte = (uint8_t)(operation & 127U);
    operation >>= 7U;
    data[at++] = operation != 0U ? (uint8_t)(byte | 128U) : byte;
  }
  if (status != 0) { write_u32(data + at, (uint32_t)status); at += 4U; }
  *length = at;
  return WL_OK;
}

wl_err_t wl_kernel_rpc_read(const uint8_t *data, size_t length, int request,
                            wl_kernel_rpc_metadata_t *out) {
  if (data == NULL || out == NULL) return WL_ERR_INVALID_ARG;
  if (length < 9U) return WL_ERR_CORRUPT_PAYLOAD;
  uint64_t session = ((uint64_t)read_u32(data) << 32U) | read_u32(data + 4U);
  uint8_t byte = data[8];
  int rejection = (byte & 1U) != 0U;
  uint32_t operation = (byte >> 1U) & 63U;
  size_t at = 9U;
  for (unsigned shift = 6U; (byte & 128U) != 0U; shift += 7U) {
    if (at == length || shift > 27U) return WL_ERR_CORRUPT_PAYLOAD;
    byte = data[at++];
    if ((shift == 27U && byte > 31U) || byte == 0U) return WL_ERR_CORRUPT_PAYLOAD;
    operation |= (uint32_t)(byte & 127U) << shift;
  }
  if (session == 0U || operation == 0U || (rejection && request))
    return WL_ERR_CORRUPT_PAYLOAD;
  int32_t status = 0;
  if (rejection) {
    if (length - at != 4U) return WL_ERR_CORRUPT_PAYLOAD;
    uint32_t bits = read_u32(data + at);
    status = bits <= INT32_MAX ? (int32_t)bits : -1 - (int32_t)(UINT32_MAX - bits);
    if (status == 0) return WL_ERR_CORRUPT_PAYLOAD;
    at += 4U;
  }
  *out = (wl_kernel_rpc_metadata_t){session, operation, status, data + at, length - at};
  return WL_OK;
}

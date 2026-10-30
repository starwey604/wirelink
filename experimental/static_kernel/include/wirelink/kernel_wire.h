/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_KERNEL_WIRE_H_
#define WIRELINK_KERNEL_WIRE_H_

#include <stddef.h>
#include <stdint.h>
#include "wirelink/types.h"

#define WL_KERNEL_NATIVE_HEADER 4U
#define WL_KERNEL_RPC_HEADER_MAX 17U

typedef struct {
  uint16_t message_id;
  const uint8_t *payload;
  size_t length;
} wl_kernel_frame_t;

typedef struct {
  uint64_t session;
  uint32_t operation;
  int32_t status;
  const uint8_t *body;
  size_t length;
} wl_kernel_rpc_metadata_t;

/* Selected native/NONE connector. No envelope or integrity selector, copying,
 * CRC or COBS. Generated codecs write the body after the four-byte prefix.
 * seal writes only that prefix, after validating all arguments and capacity.
 * read leaves the output unchanged on error; reliable DATA/ACK are unsupported. */
wl_err_t wl_kernel_native_none_seal(uint8_t *, size_t, uint16_t, size_t);
wl_err_t wl_kernel_native_none_read(const uint8_t *, size_t, wl_kernel_frame_t *);

/* Managed RPC v4 metadata, identical to the old generated managed wire.
 * request != 0 rejects business rejection metadata. read validates canonical
 * operation encoding and forbids trailing business bytes on a rejection. */
wl_err_t wl_kernel_rpc_read(const uint8_t *, size_t, int request,
                            wl_kernel_rpc_metadata_t *);
size_t wl_kernel_rpc_header_size(uint32_t operation, int32_t status);
/* Writes only metadata, resetting *length to zero on failure. Status != 0
 * represents rejection and must be used with no business body. */
wl_err_t wl_kernel_rpc_write(uint8_t *, size_t, uint64_t, uint32_t, int32_t,
                             size_t *length);

#endif

/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_SRC_LINK_FRAME_H_
#define WIRELINK_SRC_LINK_FRAME_H_
#include "context.h"
#include "frame_internal.h"

/* Keep owner admission and storage decisions common across capabilities. The
 * fixed library has one wire kernel; its unavailable-mode entries only reject.
 * No per-byte selector, callback, or alternate fixed wire engine exists. */
WL_FRAME_INLINE int wl_link_encode(wl_ctx_t *ctx, const wl_wire_packet_t *packet,
                                  uint8_t *out, size_t cap, size_t *length) {
  const wl_config_t *config = &wl_ctx_impl(ctx)->config;
  if (config->envelope != WL_ENVELOPE_COBS_STREAM ||
      config->integrity != WL_INTEGRITY_CRC16)
    return wl_frame_encode_generic(config, packet, out, cap, length);
  return wl_frame_encode_cobs_crc16(config, packet, out, cap, length);
}
WL_FRAME_INLINE int wl_link_decode_stream(wl_ctx_t *ctx, uint8_t *encoded,
                                         size_t length, wl_frame_view_t *view) {
  const wl_config_t *config = &wl_ctx_impl(ctx)->config;
  if (config->integrity != WL_INTEGRITY_CRC16)
    return wl_frame_decode_cobs_generic(config, encoded, length, view);
  return wl_frame_decode_cobs_crc16(config, encoded, length, view);
}
WL_FRAME_INLINE void wl_link_process_rx(wl_ctx_t *ctx) {
  if (wl_ctx_impl(ctx)->config.envelope != WL_ENVELOPE_COBS_STREAM) {
    wl_frame_process_units(ctx);
    return;
  }
  wl_frame_process_stream(ctx);
}
#endif /* WIRELINK_SRC_LINK_FRAME_H_ */

/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "context.h"
#include "frame_internal.h"
#include "unit_rx.h"
#if !WL_LINK_PROFILE_COBS_CRC16
static int wl_feed_unit_raw(wl_ctx_t *ctx, const uint8_t *unit, size_t len);
static int wl_feed_parse_wire(wl_ctx_t *ctx,
                                                  const uint8_t *data,
                                                  size_t len) {
  wl_frame_view_t view;
  if (ctx == NULL || data == NULL || len == 0U) return WL_ERR_INVALID_ARG;
  int ret = wl_frame_decode(data, len, wl_link_integrity(&wl_ctx_impl(ctx)->config), &view);
  return ret == WL_OK ? wl_frame_route_view(ctx, &view) : wl_frame_bad_frame(ctx, ret);
}
static int wl_feed_unit_raw(wl_ctx_t *ctx, const uint8_t *unit, size_t len) {
  size_t raw_len;
  if (ctx == NULL || unit == NULL) {
    return WL_ERR_INVALID_ARG;
  }
  if (wl_link_envelope(&wl_ctx_impl(ctx)->config) == WL_ENVELOPE_BUS_LENGTH16) {
    if (len < 2U) {
      wl_ctx_impl(ctx)->rx_counters.malformed++;
      return WL_ERR_BAD_FRAME;
    }
    raw_len = ((size_t)unit[0] << 8U) | unit[1];
    if (raw_len + 2U > len ||
        (wl_ctx_impl(ctx)->config.max_transmission_unit != 0U &&
         raw_len + 2U > wl_ctx_impl(ctx)->config.max_transmission_unit)) {
      wl_ctx_impl(ctx)->rx_counters.malformed++;
      return WL_ERR_BAD_FRAME;
    }
    for (size_t i = raw_len + 2U; i < len; ++i) {
      if (unit[i] != 0U) {
        wl_ctx_impl(ctx)->rx_counters.malformed++;
        return WL_ERR_BAD_FRAME;
      }
    }
    return wl_feed_parse_wire(ctx, unit + 2U, raw_len);
  }
  return wl_feed_parse_wire(ctx, unit, len);
}
void wl_frame_process_units(wl_ctx_t *ctx) {
  if (wl_ctx_impl(ctx)->rx_units.initialized == 0U) return;
  while (wl_ctx_impl(ctx)->has_event == 0U &&
         wl_ctx_impl(ctx)->rx_event_leased == 0U) {
    wl_span_t unit = wl_rx_unit_consumer_peek(ctx);
    int ret;

    if (unit.data == NULL || unit.length == 0U) {
      return;
    }
    wl_ctx_impl(ctx)->rx_candidate_source = WL_RX_SOURCE_UNIT;
    ret = wl_feed_unit_raw(ctx, unit.data, unit.length);
    if (wl_ctx_impl(ctx)->has_event != 0U &&
        (wl_ctx_impl(ctx)->event.type == WL_EVT_UNRELIABLE_RX ||
         wl_ctx_impl(ctx)->event.type == WL_EVT_RELIABLE_RX)) {
      wl_ctx_impl(ctx)->rx_pending_consume = 1U;
      return;
    }
    (void)wl_rx_unit_consumer_consume(ctx);
    wl_ctx_impl(ctx)->rx_candidate_source = WL_RX_SOURCE_NONE;
    if (wl_ctx_impl(ctx)->has_event != 0U) {
      return;
    }
    (void)ret;
  }
}
int wl_frame_receive_unit(wl_ctx_t *ctx, const uint8_t *unit, size_t len) {
  if (wl_ctx_impl(ctx)->has_event != 0U || wl_ctx_impl(ctx)->rx_event_leased != 0U) {
    return WL_ERR_WOULD_BLOCK;
  }
  if (len > wl_ctx_impl(ctx)->storage.rx_fallback_size) {
    return WL_ERR_PAYLOAD_TOO_LONG;
  }
  if (len != 0U) {
    memcpy(wl_ctx_impl(ctx)->storage.rx_fallback, unit, len);
  }
  wl_ctx_impl(ctx)->rx_candidate_source = WL_RX_SOURCE_FALLBACK;
  return wl_feed_unit_raw(ctx, wl_ctx_impl(ctx)->storage.rx_fallback, len);
}
#else
void wl_frame_process_units(wl_ctx_t *ctx) { (void)ctx; }
int wl_frame_receive_unit(wl_ctx_t *ctx, const uint8_t *unit, size_t len) {
  (void)ctx; (void)unit; (void)len;
  return WL_ERR_NOT_SUPPORTED;
}
#endif

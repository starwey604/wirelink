/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_FRAMING_WORKLOAD_H
#define WIRELINK_FRAMING_WORKLOAD_H
#include "wirelink/frame.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { FRAMING_SPARE, FRAMING_EXACT, FRAMING_OVERLAP,
       FRAMING_RAW_DECODE, FRAMING_COBS_DECODE, FRAMING_MODES };
typedef struct {
  wl_wire_packet_t packet;
  wl_frame_view_t rx_view;
  wl_envelope_type_t envelope;
  unsigned mode;
  uint64_t calls; /* Always zero: standalone tools have no sink or engine. */
  size_t exact_len, raw_len, output_len;
  uint8_t payload[WL_FRAME_MAX_PAYLOAD];
  uint8_t tx_unit[WL_FRAME_MAX_COBS_LEN];
  uint8_t expected[WL_FRAME_MAX_COBS_LEN];
  uint8_t raw[WL_FRAME_MAX_RAW_LEN];
} framing_fixture_t;
int framing_init(framing_fixture_t *f, unsigned mode, wl_envelope_type_t envelope,
                 wl_integrity_t integrity, unsigned pattern, size_t length);
int framing_step(framing_fixture_t *f);
int framing_check(framing_fixture_t *f);
const char *framing_mode_name(unsigned mode);
#ifdef __cplusplus
}
#endif
#endif

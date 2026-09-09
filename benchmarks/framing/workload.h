/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_FRAMING_WORKLOAD_H
#define WIRELINK_FRAMING_WORKLOAD_H
#include "wirelink/frame.h"
#include "wirelink/wirelink.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { FRAMING_SPARE, FRAMING_EXACT, FRAMING_OVERLAP, FRAMING_SEND,
       FRAMING_CLAIM, FRAMING_BUSY3, FRAMING_RETRY3, FRAMING_MODES };
typedef struct {
  wl_ctx_t link;
  wl_wire_packet_t packet;
  wl_envelope_type_t envelope;
  unsigned mode, busy;
  uint32_t sequence, now;
  uint64_t calls;
  wl_io_token_t token;
  const uint8_t *last_data;
  size_t last_len, exact_len, output_len;
  uint8_t payload[WL_FRAME_MAX_PAYLOAD];
  uint8_t tx_payload[WL_FRAME_MAX_PAYLOAD];
  uint8_t tx_unit[WL_FRAME_MAX_COBS_LEN];
  uint8_t control[WL_FRAME_MAX_COBS_LEN];
  uint8_t fifo[WL_FRAME_MAX_COBS_LEN];
  uint8_t fallback[WL_FRAME_MAX_COBS_LEN];
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

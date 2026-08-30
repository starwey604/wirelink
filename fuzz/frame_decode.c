/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "wirelink/frame.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  uint8_t encoded[WL_FRAME_MAX_RAW_LEN];
  wl_frame_view_t view = {0};
  wl_wire_packet_t packet;
  wl_integrity_t integrity;
  size_t encoded_len = 0U;

  if (size == 0U || size - 1U > WL_FRAME_MAX_RAW_LEN) {
    return 0;
  }
  integrity = (wl_integrity_t)(data[0] % 3U);
  if (wl_frame_decode(data + 1U, size - 1U, integrity, &view) != WL_OK) {
    return 0;
  }
  packet = (wl_wire_packet_t){
      .type = view.type,
      .integrity = integrity,
      .flags = view.flags,
      .message_id = view.message_id,
      .session_id = view.session_id,
      .sequence = view.sequence,
      .payload = view.payload.data,
      .payload_len = view.payload.length,
  };
  if (wl_frame_encode(&packet, WL_ENVELOPE_NATIVE_PACKET, encoded,
                      sizeof(encoded), &encoded_len) != WL_OK ||
      encoded_len != size - 1U ||
      memcmp(encoded, data + 1U, encoded_len) != 0) {
    abort();
  }
  return 0;
}

/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/kernel_wire.h"
#include "wirelink/frame.h"

wl_err_t wl_kernel_native_none_seal(uint8_t *frame, size_t capacity,
                                    uint16_t message_id, size_t payload_length) {
  if (frame == NULL || message_id == 0U) return WL_ERR_INVALID_ARG;
  if (payload_length > WL_FRAME_MAX_PAYLOAD) return WL_ERR_PAYLOAD_TOO_LONG;
  if (capacity < WL_KERNEL_NATIVE_HEADER + payload_length) return WL_ERR_BUF_TOO_SMALL;
  frame[0] = WL_FRAME_PREFIX;
  frame[1] = 0U;
  frame[2] = (uint8_t)(message_id >> 8U);
  frame[3] = (uint8_t)message_id;
  return WL_OK;
}

wl_err_t wl_kernel_native_none_read(const uint8_t *frame, size_t length,
                                    wl_kernel_frame_t *out) {
  if (frame == NULL || out == NULL) return WL_ERR_INVALID_ARG;
  if (length < WL_KERNEL_NATIVE_HEADER) return WL_ERR_BAD_FRAME;
  if ((frame[0] & WL_FRAME_MARKER_MASK) != WL_FRAME_MARKER) return WL_ERR_BAD_FRAME;
  if ((frame[0] & WL_FRAME_VERSION_MASK) !=
      (WL_FRAME_VERSION << WL_FRAME_VERSION_SHIFT)) return WL_ERR_PROTOCOL_VERSION;
  if ((frame[0] & WL_FRAME_KIND_MASK) != 0U) return WL_ERR_NOT_SUPPORTED;
  if (length - WL_KERNEL_NATIVE_HEADER > WL_FRAME_MAX_PAYLOAD)
    return WL_ERR_FRAME_TOO_LONG;
  uint16_t id = (uint16_t)((uint16_t)frame[2] << 8U) | frame[3];
  if (frame[1] != 0U || id == 0U) return WL_ERR_BAD_FRAME;
  *out = (wl_kernel_frame_t){id, frame + WL_KERNEL_NATIVE_HEADER,
                            length - WL_KERNEL_NATIVE_HEADER};
  return WL_OK;
}

/* SPDX-License-Identifier: Apache-2.0 */

#include "wirelink/cobs.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "wirelink/types.h"

size_t wl_cobs_encoded_max_size(size_t input_len) {
  if (input_len == 0U) return 1U;
  if (input_len > SIZE_MAX - input_len / 254U - 1U) return SIZE_MAX;
  return input_len + input_len / 254U + 1U;
}

int wl_cobs_encode(const uint8_t *input, size_t input_len, uint8_t *output,
                   size_t output_capacity, size_t *output_len) {
  if (output_len == NULL) return WL_ERR_INVALID_ARG;
  *output_len = 0U;
  if (input == NULL && input_len > 0U) return WL_ERR_INVALID_ARG;
  if (output_capacity == 0U) return WL_ERR_BUF_TOO_SMALL;
  if (output == NULL) return WL_ERR_INVALID_ARG;

  size_t code_idx = 0U, write = 1U;
  uint8_t code = 1U;
  for (size_t i = 0U; i < input_len; ++i) {
    if (input[i] == 0U) {
      output[code_idx] = code;
      code = 1U;
      if (write >= output_capacity) return WL_ERR_BUF_TOO_SMALL;
      code_idx = write++;
      continue;
    }
    if (write >= output_capacity) return WL_ERR_BUF_TOO_SMALL;
    output[write++] = input[i];
    if (++code == 0xFFU) {
      output[code_idx] = code;
      code = 1U;
      if (write >= output_capacity) return WL_ERR_BUF_TOO_SMALL;
      code_idx = write++;
    }
  }
  output[code_idx] = code;
  *output_len = write;
  return WL_OK;
}

int wl_cobs_decode(const uint8_t *input, size_t input_len, uint8_t *output,
                   size_t output_capacity, size_t *output_len) {
  if (output_len == NULL) return WL_ERR_INVALID_ARG;
  *output_len = 0U;
  if (input == NULL && input_len > 0U) return WL_ERR_INVALID_ARG;
  if (input_len == 0U) return WL_ERR_COBS_DECODE;
  if (output == NULL && output_capacity > 0U) return WL_ERR_INVALID_ARG;

  size_t in_pos = 0U, out_pos = 0U;
  while (in_pos < input_len) {
    uint8_t code = input[in_pos++];
    if (code == 0U) return WL_ERR_COBS_DECODE;
    size_t copy = (size_t)(code - 1U);
    if (copy > input_len - in_pos) return WL_ERR_COBS_DECODE;
    if (copy > output_capacity - out_pos) return WL_ERR_BUF_TOO_SMALL;
    if (copy != 0U) {
      memmove(output + out_pos, input + in_pos, copy);
      in_pos += copy;
      out_pos += copy;
    }
    if (in_pos < input_len && code != 0xFFU) {
      if (out_pos >= output_capacity) return WL_ERR_BUF_TOO_SMALL;
      output[out_pos++] = 0U;
    }
  }
  *output_len = out_pos;
  return WL_OK;
}

int wl_cobs_decode_in_place(uint8_t *buffer, size_t input_len,
                            size_t *output_len) {
  if (output_len == NULL) return WL_ERR_INVALID_ARG;
  *output_len = 0U;
  if (input_len == 0U) return WL_ERR_COBS_DECODE;
  if (buffer == NULL) return WL_ERR_INVALID_ARG;

  /* After a normal block, its successor's code occupies exactly the
   * decoded zero's place. Patch it and slide adjacent blocks together;
   * only a full 254-byte block opens another hole in the encoded input. */
  size_t head = 0U, first = 1U, out_pos = 0U;
  uint8_t code = buffer[0];
  for (;;) {
    if (code == 0U || (size_t)code > input_len - head) return WL_ERR_COBS_DECODE;
    size_t next = head + code;
    if (next == input_len) break;
    uint8_t next_code = buffer[next];
    if (code == 0xffU) {
      size_t n = next - first;
      memmove(buffer + out_pos, buffer + first, n);
      out_pos += n;
      first = next + 1U;
    } else {
      buffer[next] = 0U;
    }
    code = next_code;
    head = next;
  }
  size_t n = input_len - first;
  memmove(buffer + out_pos, buffer + first, n);
  out_pos += n;
  *output_len = out_pos;
  return WL_OK;
}

/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "wirelink/cobs.h"

enum { MAX_INPUT = 4096 };

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  uint8_t in_place[MAX_INPUT];
  uint8_t separate[MAX_INPUT];
  size_t in_place_len = 0U;
  size_t separate_len = 0U;
  int in_place_result;
  int separate_result;

  if (size > MAX_INPUT) {
    return 0;
  }
  memcpy(in_place, data, size);
  in_place_result = wl_cobs_decode_in_place(in_place, size, &in_place_len);
  separate_result =
      wl_cobs_decode(data, size, separate, sizeof(separate), &separate_len);
  if (in_place_result != separate_result || in_place_len != separate_len ||
      (in_place_result == WL_OK &&
       memcmp(in_place, separate, separate_len) != 0)) {
    abort();
  }
  return 0;
}

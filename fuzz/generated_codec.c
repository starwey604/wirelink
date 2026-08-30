/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "control.h"

enum { MAX_INPUT = 4096, MAX_JOINTS = 8 };

static int points_into(const void *pointer, size_t length,
                       const uint8_t *input, size_t input_length) {
  const uintptr_t address = (uintptr_t)pointer;
  const uintptr_t begin = (uintptr_t)input;
  return address >= begin && length <= input_length &&
         address - begin <= input_length - length;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  arm_command_t decoded = {0};
  joint_command_t joints[MAX_JOINTS] = {0};

  if (size > MAX_INPUT) {
    return 0;
  }
  decoded.joints = joints;
  decoded.joints_capacity = MAX_JOINTS;
  if (arm_command_decode(data, size, &decoded) != WL_CODEC_OK) {
    return 0;
  }
  if ((decoded.has_source &&
       !points_into(decoded.source.data, decoded.source.length, data, size)) ||
      (decoded.has_extension &&
       !points_into(decoded.extension.data, decoded.extension.length, data,
                    size)) ||
      decoded.joints_count > MAX_JOINTS) {
    abort();
  }
  return 0;
}

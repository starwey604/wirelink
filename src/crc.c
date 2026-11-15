/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>

#include "wirelink/crc.h"

#include "crc_internal.h"

/* 16-entry nibble table (32 B = one 32 B cache line) for CRC-16/CCITT-FALSE. */
static const uint16_t wl_crc16_nibble_table[16] = {
    0x0000U, 0x1021U, 0x2042U, 0x3063U, 0x4084U, 0x50A5U, 0x60C6U, 0x70E7U,
    0x8108U, 0x9129U, 0xA14AU, 0xB16BU, 0xC18CU, 0xD1ADU, 0xE1CEU, 0xF1EFU,
};

uint16_t wl_crc16_ccitt_false_update(uint16_t state, const uint8_t *data,
                                     size_t length) {
  for (size_t i = 0U; i < length; ++i) {
    uint8_t value = data[i];
    state = (uint16_t)((state << 4U) ^
                       wl_crc16_nibble_table[(state >> 12U) ^ (value >> 4U)]);
    state = (uint16_t)((state << 4U) ^
                       wl_crc16_nibble_table[(state >> 12U) ^ (value & 0x0FU)]);
  }
  return state;
}

uint16_t wl_crc16_ccitt_false(const uint8_t *data, size_t length) {
  if (data == NULL && length != 0U) return 0U;
  return wl_crc16_ccitt_false_update(0xFFFFU, data, length);
}

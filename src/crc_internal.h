/* SPDX-License-Identifier: Apache-2.0 */

#ifndef SRC_CRC_INTERNAL_H_
#define SRC_CRC_INTERNAL_H_

#include <stddef.h>
#include <stdint.h>

extern const uint16_t wl_crc16_table[256];

static inline uint16_t wl_crc16_byte(uint16_t state, uint8_t value) {
  return (uint16_t)((uint16_t)(state << 8U) ^
                    wl_crc16_table[(uint8_t)((state >> 8U) ^ value)]);
}

/*
 * Incremental CRC state helpers used when a wire frame is held in disjoint
 * spans. The CRC-32C state is unfinalized; callers apply the final XOR after
 * the last span.
 */
uint16_t wl_crc16_ccitt_false_update(uint16_t state, const uint8_t *data,
                                     size_t length);
uint32_t wl_crc32c_update(uint32_t state, const uint8_t *data, size_t length);

#endif /* SRC_CRC_INTERNAL_H_ */

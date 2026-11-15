/* SPDX-License-Identifier: Apache-2.0 */

#ifndef SRC_CRC_INTERNAL_H_
#define SRC_CRC_INTERNAL_H_

#include <stddef.h>
#include <stdint.h>

uint16_t wl_crc16_ccitt_false_update(uint16_t state, const uint8_t *data,
                                     size_t length);

#endif /* SRC_CRC_INTERNAL_H_ */

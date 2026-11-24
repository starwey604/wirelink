/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WL_TEST_PACKET_PORT_ABI_H
#define WL_TEST_PACKET_PORT_ABI_H
#include "wirelink/packet_port.h"

/* Keep every member in declaration order: both language probes use this list. */
#define WL_TEST_PORT_MEMBERS(X) \
  X(config) X(write_cursor) X(read_cursor) X(tx_state) X(running) \
  X(claim_active) X(activity) X(rx_mask) X(lengths) X(rx_sequences) X(claim_sequence) \
  X(claim_cursor) X(claim_capacity) X(tx_sequence) X(tx_delivered) X(borrowed)
#ifdef __cplusplus
extern "C" {
#endif
const size_t *cpp_test_port_layout(size_t *count);
#ifdef __cplusplus
}
#endif
#endif

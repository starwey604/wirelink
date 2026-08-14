#ifndef INCLUDE_WIRELINK_RX_RING_STATE_H_
#define INCLUDE_WIRELINK_RX_RING_STATE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Backend-private RX ring state.  Keeping this storage opaque prevents a
 * selected third-party backend from becoming part of Wirelink's public ABI.
 */
#define WL_RX_RING_STATE_SIZE 128U

typedef union {
  max_align_t align;
  uint8_t bytes[WL_RX_RING_STATE_SIZE];
} wl_rx_ring_state_t;

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_WIRELINK_RX_RING_STATE_H_ */

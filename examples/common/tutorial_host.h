/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_TUTORIAL_HOST_H
#define WIRELINK_TUTORIAL_HOST_H
#include "wirelink/kernel.h"
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Desktop example support only. C owns the port/core/RX storage; the C++
 * wrapper owns socket/wait resources. No RPC state or hidden owner thread. */
typedef struct example_udp example_udp_t;
uint32_t example_now_ms(void);
int example_running(void);
int example_int32(const char *, int32_t *);
example_udp_t *example_udp_open_at(wl_packet_port_t *, wl_span_t rx, size_t unit_size,
    const char *local_address, uint16_t local, const char *peer_address, uint16_t peer);
wl_err_t example_udp_step(example_udp_t *, wl_kernel_t *, uint32_t now);
wl_err_t example_udp_wait(example_udp_t *, wl_kernel_t *, uint32_t maximum_ms);
/* Same owner: close core, stop/join real I/O, drain actual leases, then free. */
wl_err_t example_udp_close(example_udp_t *, wl_kernel_t *);
#define CHECK(expression) do { if (!(expression)) { \
  fprintf(stderr, "line %d: %s\n", __LINE__, #expression); return 1; \
} } while (0)
#ifdef __cplusplus
}
#endif
#endif

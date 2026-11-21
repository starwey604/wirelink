/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_TUTORIAL_HOST_H
#define WIRELINK_TUTORIAL_HOST_H
#include "wirelink/endpoint.h"
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
wl_err_t example_instance(uint32_t *instance);
example_udp_t *example_udp_create(wl_packet_port_t *port);
const wl_packet_io_t *example_udp_io(const example_udp_t *udp);
wl_err_t example_udp_start(example_udp_t *udp, size_t tx_max, int argc, char **argv,
                           uint16_t default_local, uint16_t default_peer);
void example_udp_destroy(example_udp_t *udp);
wl_err_t example_udp_step(example_udp_t *, wl_endpoint_t *, uint32_t now);
wl_err_t example_udp_wait(example_udp_t *, wl_endpoint_t *, uint32_t maximum_ms);
/* Same owner: close core, stop/join real I/O, drain actual leases, then free. */
wl_err_t example_udp_close(example_udp_t *, wl_endpoint_t *);
#define CHECK(expression) do { if (!(expression)) { \
  fprintf(stderr, "line %d: %s\n", __LINE__, #expression); return 1; \
} } while (0)
#ifdef __cplusplus
}
#endif
#endif

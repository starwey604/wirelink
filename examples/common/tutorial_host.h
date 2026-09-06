/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_TUTORIAL_HOST_H
#define WIRELINK_TUTORIAL_HOST_H

#include <stdint.h>
#include <stdio.h>
#include "wirelink/endpoint.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Example support, not a Wirelink core API. The C++ implementation owns the
 * desktop UDP adapter; business examples remain ordinary C11 programs. */
typedef struct example_udp example_udp_t;
uint64_t example_session_id(void);
wl_time_ms_t example_now_ms(void);
int example_running(void);
int example_ports(int argc, char **argv, uint16_t *local, uint16_t *peer);
int example_int32(const char *text, int32_t *value);
example_udp_t *example_udp_open(wl_endpoint_t *endpoint, uint16_t local, uint16_t peer);
int example_udp_wait(example_udp_t *udp, uint32_t maximum_ms);
/* Closes the attached endpoint, then frees desktop resources. */
void example_udp_close(example_udp_t *udp);

#define CHECK(expression) do { \
  if (!(expression)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expression); \
    return 1; \
  } \
} while (0)

#ifdef __cplusplus
}
#endif
#endif

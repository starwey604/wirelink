/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_TUTORIAL_HOST_H
#define WIRELINK_TUTORIAL_HOST_H
#include "wirelink/clock.h"
#include "wirelink/udp.h"
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
wl_clock_t example_clock(void);
uint32_t example_now_ms(void);
void example_install_signals(void);
bool example_running(void);
wl_err_t example_instance(uint32_t *instance);
bool example_udp_args(int argc, char **argv, wl_udp_config_t *config);
#ifdef __cplusplus
}
#endif
#endif

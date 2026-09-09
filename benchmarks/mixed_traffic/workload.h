/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MIXED_TRAFFIC_WORKLOAD_H_
#define MIXED_TRAFFIC_WORKLOAD_H_
#include <stdbool.h>
int mixed_traffic_run(bool check);
#ifdef MIXED_TRAFFIC_H7
/* Test-only instrumentation; ordinary deterministic builds have no hooks. */
void mixed_h7_case_begin(void);
void mixed_h7_tick_begin(void);
void mixed_h7_tick_end(void);
void mixed_h7_case_end(const char *scenario, int envelope);
#endif
#endif

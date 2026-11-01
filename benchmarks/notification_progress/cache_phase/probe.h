/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WL_RESEARCH_CACHE_PHASE_H
#define WL_RESEARCH_CACHE_PHASE_H
#include <stddef.h>
#include <stdint.h>
void probe_init(void);
void probe_start(void);
void probe_stop(void);
size_t probe_config(char *data, size_t capacity, unsigned mode);
size_t probe_json(char *data, size_t capacity, unsigned page);
#endif

/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WL_RESEARCH_PMU_SAMPLE_H
#define WL_RESEARCH_PMU_SAMPLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "probe_hw.h"
typedef struct {
  uint32_t index, clock_start, cycles, window, stall, miss;
  uint32_t base_before, start_before, base_after, start_after, outer_window;
} pmu_record_t;
extern unsigned pmu_mode, pmu_record_count;
extern uint32_t pmu_attempts, pmu_busy, pmu_errors;
extern pmu_record_t pmu_records[16];
#define PMU_INDEX(index) (((index) & 63U) == ((((index) >> 6U) * 17U + 17U) & 63U))
#define PMU_SELECTED(index) (pmu_mode == 1U && PMU_INDEX(index))
/* Outside the original elapsed-clock scope; owner-only, no function calls. */
#define PMU_FINISH(index_, start_, cycles_, status_, picked_, before_, after_, outer_) do { \
  ++pmu_attempts; \
  if ((status_) == 1) ++pmu_busy; \
  else if ((status_) != 0) ++pmu_errors; \
  if ((picked_) && (status_) == 0 && pmu_record_count < 16U) { \
    pmu_record_t *pmu_r_ = &pmu_records[pmu_record_count++]; \
    pmu_r_->index = (index_); pmu_r_->clock_start = (start_); \
    pmu_r_->cycles = (cycles_); \
    pmu_r_->window = (after_).window - (before_).window; \
    pmu_r_->stall = (after_).stall - (before_).stall; \
    pmu_r_->miss = (after_).miss - (before_).miss; \
    pmu_r_->base_before = (before_).base; pmu_r_->start_before = (before_).start; \
    pmu_r_->base_after = (after_).base; pmu_r_->start_after = (after_).start; \
    pmu_r_->outer_window = (outer_); \
  } \
} while (0)
void probe_init(void);
void probe_start(void);
void probe_stop(void);
size_t probe_config(char *data, size_t capacity, unsigned mode);
size_t probe_json(char *data, size_t capacity, unsigned page);
#endif

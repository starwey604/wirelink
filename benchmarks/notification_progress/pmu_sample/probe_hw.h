/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WL_RESEARCH_PMU_HW_H
#define WL_RESEARCH_PMU_HW_H
#include <stdint.h>
#ifdef PMU_PROBE_TEST
#include "pmu_fake.h"
#else
#include <soc/soc.h>
#include <soc/extmem_reg.h>
#include <xtensa-debug-module.h>
#include <xtensa/xt_perf_consts.h>
#include <zephyr/kernel.h>
/* Macros keep the sparse snapshots free of extra CALL8/CALLX8 frames. */
#define PMU_READ(address, result) do { \
  uint32_t pmu_address_ = (address); \
  __asm__ volatile("rer %0, %1" : "=r"(result) : "r"(pmu_address_) : "memory"); \
} while (0)
#define PMU_WRITE(address, value) do { \
  uint32_t pmu_address_ = (address), pmu_value_ = (value); \
  __asm__ volatile("wer %0, %1" :: "r"(pmu_value_), "r"(pmu_address_) : "memory"); \
} while (0)
#define PMU_CLOCK(result) __asm__ volatile("rsr.ccount %0" : "=r"(result) :: "memory")
#define PMU_WINDOWS(base, start) \
  __asm__ volatile("rsr.windowbase %0\n\trsr.windowstart %1" \
      : "=r"(base), "=r"(start) :: "memory")
#define PMU_MISS() REG_READ(EXTMEM_IBUS_ACS_MISS_CNT_REG)
#define PMU_ACCESS() REG_READ(EXTMEM_IBUS_ACS_CNT_REG)
#endif

#define PMU_CONTROL(select, mask) \
  ((15U << PMCTRL_TRACELEVEL_SHIFT) | ((select) << PMCTRL_SELECT_SHIFT) | \
   ((mask) << PMCTRL_MASK_SHIFT))
#define PMU_WINDOW_CONTROL PMU_CONTROL(XTPERF_CNT_EXR, XTPERF_MASK_EXR_WINDOW)
#define PMU_STALL_CONTROL PMU_CONTROL(XTPERF_CNT_I_STALL, XTPERF_MASK_I_STALL_ALL)
typedef struct {
  uint32_t window, stall, miss, base, start;
} pmu_snapshot_t;
#define PMU_SNAPSHOT(value) do { \
  PMU_READ(ERI_PERFMON_PM0, (value).window); \
  PMU_READ(ERI_PERFMON_PM1, (value).stall); \
  (value).miss = PMU_MISS(); \
  PMU_WINDOWS((value).base, (value).start); \
} while (0)
#endif

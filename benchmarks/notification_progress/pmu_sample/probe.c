/* SPDX-License-Identifier: Apache-2.0 */
#include "probe.h"
#include <stdio.h>
#include <limits.h>

unsigned pmu_mode, pmu_record_count;
uint32_t pmu_attempts, pmu_busy, pmu_errors;
pmu_record_t pmu_records[16];
static bool active, calibrated;
static unsigned epoch, calibration_error;
static uint32_t prior_pgm, clock_span, cycle_span, shallow, deep, divide_stall;
static uint32_t empty_cycles_max, empty_windows_max, empty_stalls_max, empty_outer_max;
static uint32_t phase_window, phase_stall, phase_miss, phase_access;
static pmu_snapshot_t phase_begin;
static uint32_t access_begin;
extern unsigned pmu_cal_depth(unsigned depth);
extern unsigned pmu_cal_divide(unsigned iterations);

static void configure(uint32_t first, uint32_t second) {
  PMU_WRITE(ERI_PERFMON_PGM, 0U);
  PMU_WRITE(ERI_PERFMON_PM0, 0U);
  PMU_WRITE(ERI_PERFMON_PM1, 0U);
  PMU_WRITE(ERI_PERFMON_PMCTRL0, first);
  PMU_WRITE(ERI_PERFMON_PMCTRL0 + 4U, second);
  PMU_WRITE(ERI_PERFMON_PGM, PGM_PMEN);
}

void probe_init(void) {
  PMU_READ(ERI_PERFMON_PGM, prior_pgm);
  if (prior_pgm != 0U) { calibration_error = 1U; return; }
  unsigned key = irq_lock();
  uint32_t c0, c1, p0, p1;
  configure(PMU_CONTROL(XTPERF_CNT_CYCLES, XTPERF_MASK_CYCLES),
            PMU_CONTROL(XTPERF_CNT_CYCLES, XTPERF_MASK_CYCLES));
  PMU_CLOCK(c0); PMU_READ(ERI_PERFMON_PM0, p0);
  (void)pmu_cal_divide(4096U);
  PMU_READ(ERI_PERFMON_PM0, p1); PMU_CLOCK(c1);
  clock_span = c1 - c0; cycle_span = p1 - p0;
  if (clock_span < 1000U || cycle_span > clock_span + 256U ||
      cycle_span < clock_span - clock_span / 5U) calibration_error = 2U;
  configure(PMU_WINDOW_CONTROL, PMU_STALL_CONTROL);
  PMU_READ(ERI_PERFMON_PM0, p0);
  unsigned checksum = 0U;
  for (unsigned i = 0; i < 32U; ++i) checksum += pmu_cal_depth(1U);
  PMU_READ(ERI_PERFMON_PM0, p1); shallow = p1 - p0;
  PMU_READ(ERI_PERFMON_PM0, p0);
  for (unsigned i = 0; i < 32U; ++i) checksum += pmu_cal_depth(32U);
  PMU_READ(ERI_PERFMON_PM0, p1); deep = p1 - p0;
  PMU_READ(ERI_PERFMON_PM1, p0);
  (void)pmu_cal_divide(256U);
  PMU_READ(ERI_PERFMON_PM1, p1); divide_stall = p1 - p0;
  if (checksum != 1056U || deep <= shallow + 32U || divide_stall == 0U)
    calibration_error = 3U;
  for (unsigned i = 0U; i < 32U; ++i) {
    pmu_snapshot_t a, b;
    uint32_t outside0, outside1;
    PMU_READ(ERI_PERFMON_PM0, outside0);
    c0 = k_cycle_get_32(); PMU_SNAPSHOT(a); PMU_SNAPSHOT(b); c1 = k_cycle_get_32();
    PMU_READ(ERI_PERFMON_PM0, outside1);
    if (outside1 - outside0 > empty_outer_max) empty_outer_max = outside1 - outside0;
    if (c1 - c0 > empty_cycles_max) empty_cycles_max = c1 - c0;
    if (b.window - a.window > empty_windows_max) empty_windows_max = b.window - a.window;
    if (b.stall - a.stall > empty_stalls_max) empty_stalls_max = b.stall - a.stall;
  }
  PMU_READ(ERI_PERFMON_PMCTRL0, p0);
  PMU_READ(ERI_PERFMON_PMCTRL0 + 4U, p1);
  if (empty_windows_max != 0U || p0 != PMU_WINDOW_CONTROL || p1 != PMU_STALL_CONTROL)
    calibration_error = 4U;
  calibrated = calibration_error == 0U;
  if (calibrated) {
    REG_WRITE(EXTMEM_IBUS_TO_FLASH_START_VADDR_REG, SOC_IROM_LOW);
    REG_WRITE(EXTMEM_IBUS_TO_FLASH_END_VADDR_REG, SOC_IROM_HIGH - 1U);
  } else PMU_WRITE(ERI_PERFMON_PGM, 0U);
  irq_unlock(key);
}

void probe_start(void) {
  pmu_record_count = pmu_attempts = pmu_busy = pmu_errors = 0U;
  ++epoch; active = true;
  access_begin = PMU_ACCESS();
  PMU_SNAPSHOT(phase_begin);
}
void probe_stop(void) {
  pmu_snapshot_t end;
  PMU_SNAPSHOT(end);
  phase_access = PMU_ACCESS() - access_begin;
  phase_window = end.window - phase_begin.window;
  phase_stall = end.stall - phase_begin.stall;
  phase_miss = end.miss - phase_begin.miss;
  active = false;
}
size_t probe_config(char *data, size_t capacity, unsigned value) {
  if (active || value > 1U) return 0U;
  pmu_mode = value;
  int n = snprintf(data, capacity,
      "{\"mode\":%u,\"calibrated\":%s,\"error\":%u,\"prior_pgm\":%u,"
      "\"ctrl0\":%u,\"ctrl1\":%u,\"clock\":%u,\"cycles\":%u,"
      "\"shallow\":%u,\"deep\":%u,\"divide_stall\":%u,"
      "\"empty_cycles\":%u,\"empty_windows\":%u,\"empty_stalls\":%u,\"empty_outer\":%u}",
      pmu_mode, calibrated ? "true" : "false", calibration_error, prior_pgm,
      PMU_WINDOW_CONTROL, PMU_STALL_CONTROL, clock_span, cycle_span, shallow, deep,
      divide_stall, empty_cycles_max, empty_windows_max, empty_stalls_max, empty_outer_max);
  return n > 0 && (size_t)n < capacity ? (size_t)n : 0U;
}
size_t probe_json(char *data, size_t capacity, unsigned page) {
  if (active || !calibrated) return 0U;
  int n;
  if (page == 0U) {
    n = snprintf(data, capacity,
        "{\"page\":0,\"epoch\":%u,\"mode\":%u,\"count\":%u,"
        "\"attempts\":%u,\"busy\":%u,\"errors\":%u,\"windows\":%u,"
        "\"stalls\":%u,\"access\":%u,\"miss\":%u,\"done\":%s}",
        epoch, pmu_mode, pmu_record_count, pmu_attempts, pmu_busy, pmu_errors,
        phase_window, phase_stall, phase_access, phase_miss,
        pmu_record_count ? "false" : "true");
  } else if (page <= 4U && (page - 1U) * 4U < pmu_record_count) {
    unsigned begin = (page - 1U) * 4U, end = begin + 4U;
    if (end > pmu_record_count) end = pmu_record_count;
    n = snprintf(data, capacity, "{\"page\":%u,\"records\":[", page);
    if (n < 0 || (size_t)n >= capacity) return 0U;
    for (unsigned i = begin; i < end; ++i) {
      const pmu_record_t *r = &pmu_records[i];
      int added = snprintf(data + n, capacity - (size_t)n,
          "%s[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]", i == begin ? "" : ",",
          r->index, r->clock_start, r->cycles, r->window, r->stall, r->miss,
          r->base_before, r->start_before, r->base_after, r->start_after, r->outer_window);
      if (added < 0 || (size_t)added >= capacity - (size_t)n) return 0U;
      n += added;
    }
    int added = snprintf(data + n, capacity - (size_t)n, "],\"done\":%s}",
                         end == pmu_record_count ? "true" : "false");
    if (added < 0 || (size_t)added >= capacity - (size_t)n) return 0U;
    n += added;
  } else return 0U;
  return n > 0 && (size_t)n < capacity ? (size_t)n : 0U;
}

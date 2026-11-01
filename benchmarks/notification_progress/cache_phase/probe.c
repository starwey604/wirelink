/* SPDX-License-Identifier: Apache-2.0 */
/* Private application phase boundaries only; no core, loop, driver or ISR hooks. */
#include "probe.h"
#include <stdbool.h>
#include <stdio.h>
#include <soc/soc.h>
#include <soc/extmem_reg.h>
static unsigned mode, epoch;
static bool active;
static uint32_t access_begin, miss_begin, access, miss;

void probe_init(void) {
  /* Counter address filter only: no cache mode/contents/IRAM changes. */
  REG_WRITE(EXTMEM_IBUS_TO_FLASH_START_VADDR_REG, SOC_IROM_LOW);
  REG_WRITE(EXTMEM_IBUS_TO_FLASH_END_VADDR_REG, SOC_IROM_HIGH - 1U);
}
void probe_start(void) {
  ++epoch;
  active = true;
  if (mode == 1U) {
    access_begin = REG_READ(EXTMEM_IBUS_ACS_CNT_REG);
    miss_begin = REG_READ(EXTMEM_IBUS_ACS_MISS_CNT_REG);
  }
}
void probe_stop(void) {
  if (mode == 1U) {
    access = REG_READ(EXTMEM_IBUS_ACS_CNT_REG) - access_begin;
    miss = REG_READ(EXTMEM_IBUS_ACS_MISS_CNT_REG) - miss_begin;
  } else access = miss = 0U;
  active = false;
}
size_t probe_config(char *data, size_t capacity, unsigned value) {
  if (active || value > 1U) return 0U;
  mode = value;
  int n = snprintf(data, capacity,
      "{\"mode\":%u,\"scope\":\"phase\",\"filter_start\":%u,\"filter_end\":%u}",
      mode, REG_READ(EXTMEM_IBUS_TO_FLASH_START_VADDR_REG),
      REG_READ(EXTMEM_IBUS_TO_FLASH_END_VADDR_REG));
  return n > 0 && (size_t)n < capacity ? (size_t)n : 0U;
}
size_t probe_json(char *data, size_t capacity, unsigned page) {
  if (active || page != 0U) return 0U;
  int n = snprintf(data, capacity,
      "{\"page\":0,\"epoch\":%u,\"mode\":%u,\"access\":%u,\"miss\":%u,\"done\":true}",
      epoch, mode, access, miss);
  return n > 0 && (size_t)n < capacity ? (size_t)n : 0U;
}

/* SPDX-License-Identifier: Apache-2.0 */
/* Forced into kernel/sem.c only. No SDK source edits or tracing config changes. */
#include <zephyr/kernel.h>
#include <zephyr/tracing/tracing.h>
#include "diagnostic.h"
extern struct k_sem *diag_owner_sem;
static inline void diag_sem_give(struct k_sem *sem) {
  if (sem == diag_owner_sem) { D(sem_give); if (sem->count == sem->limit) D(sem_saturated); }
}
static inline void diag_sem_take(struct k_sem *sem) {
  if (sem == diag_owner_sem) { D(sem_take); if (sem->count != 0U) D(sem_immediate); }
}
#undef SYS_PORT_TRACING_OBJ_FUNC_ENTER
#define SYS_PORT_TRACING_OBJ_FUNC_ENTER(type, func, obj, ...) diag_sem_##func(obj)
#undef SYS_PORT_TRACING_OBJ_FUNC_BLOCKING
#define SYS_PORT_TRACING_OBJ_FUNC_BLOCKING(type, func, obj, ...) \
  do { if ((obj) == diag_owner_sem) D(sem_block); } while (0)

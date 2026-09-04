/* SPDX-License-Identifier: Apache-2.0 */

#ifndef INCLUDE_WIRELINK_PUMP_H_
#define INCLUDE_WIRELINK_PUMP_H_

#include <stddef.h>
#include <stdint.h>

#include "wirelink/link.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*wl_pump_service_fn)(void *user_data);
typedef void (*wl_pump_quiesce_fn)(void *user_data);
typedef uint8_t (*wl_pump_application_progress_fn)(void *user_data,
                                                    wl_ctx_t *ctx,
                                                    wl_time_ms_t now_ms);
typedef uint32_t (*wl_pump_deadline_hint_fn)(const void *user_data,
                                             wl_time_ms_t now_ms);
typedef void (*wl_pump_event_fn)(void *user_data, wl_ctx_t *ctx,
                                 const wl_event_t *event);

typedef struct wl_pump_hooks {
  void *user_data;
  wl_pump_service_fn service;
  wl_pump_quiesce_fn quiesce;
  wl_pump_application_progress_fn application_progress;
  wl_pump_deadline_hint_fn application_deadline_hint;
  wl_pump_deadline_hint_fn adapter_deadline_hint;
  /*
   * RX callbacks must release the borrowed event exactly once. Terminal TX
   * callbacks run before the pump takes the handle. With no callback, RX is
   * released and terminal TX is reclaimed automatically.
   */
  wl_pump_event_fn on_event;
} wl_pump_hooks_t;

typedef struct wl_pump_result {
  size_t events;
  size_t rx_events;
  uint32_t poll_errors;
  uint32_t service_errors;
  int service_result;
  int poll_result;
  uint8_t progress;
} wl_pump_result_t;

/*
 * Execute one bounded owner pass. All calls, except producer-side Wirelink RX
 * APIs, remain on this owner. Service errors are reported in out_result but do
 * not prevent already-buffered protocol work from being drained.
 */
wl_err_t wl_pump_step(wl_ctx_t *ctx, wl_time_ms_t now_ms,
                      size_t event_budget, const wl_pump_hooks_t *hooks,
                      wl_pump_result_t *out_result);

/* Merge core, application, and adapter relative deadlines without side effects. */
wl_err_t wl_pump_get_hint(const wl_ctx_t *ctx, wl_time_ms_t now_ms,
                          const wl_pump_hooks_t *hooks,
                          wl_poll_hint_t *out_hint);

/* Stop adapter-side producers before owner storage is detached or destroyed. */
void wl_pump_quiesce(const wl_pump_hooks_t *hooks);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_WIRELINK_PUMP_H_ */

/* SPDX-License-Identifier: Apache-2.0 */
#ifndef INCLUDE_WIRELINK_ENDPOINT_H_
#define INCLUDE_WIRELINK_ENDPOINT_H_

#include "wirelink/pump.h"
#include "wirelink/wait.h"

#ifdef __cplusplus
extern "C" {
#endif

struct wl_rpc_executor;

/* Default owner-side assembly, normally embedded by WLC. Zero-initialize
 * before first init; never copy/move while initialized. Members are private.
 * This object creates no thread and owns no transport or external buffers. */
typedef struct wl_endpoint {
  wl_ctx_t private_link;
  wl_pump_hooks_t private_hooks;
  wl_pump_result_t private_step;
  wl_clock_t private_clock;
  wl_waiter_t private_waiter;
  const struct wl_rpc_executor *private_executor;
  wl_time_ms_t private_now;
  uint8_t private_stepping;
  uint8_t private_ready;
} wl_endpoint_t;

/* Generated lifecycle bridge; storage/transport remain owned by the caller.
 * A platform executor drives step and close, never also dispatches core events. */
typedef struct {
  wl_endpoint_t *endpoint;
  void *context;
  wl_err_t (*step)(void *context);
  wl_err_t (*close)(void *context);
} wl_endpoint_driver_t;

wl_err_t wl_endpoint_init(wl_endpoint_t *endpoint, const wl_config_t *config,
                         const wl_storage_t *storage,
                         const wl_clock_t *clock,
                         const wl_pump_hooks_t *application);
wl_ctx_t *wl_endpoint_link(wl_endpoint_t *endpoint);
uint8_t wl_endpoint_has_adapter(const wl_endpoint_t *endpoint);
/* Adapter integration: only adapter fields are installed. Application hooks
 * remain owned by the generated assembly. Attach before driving the owner. */
wl_err_t wl_endpoint_attach(wl_endpoint_t *endpoint,
                           const wl_pump_hooks_t *adapter);
/* Setup-only platform integration; a NULL descriptor disables sync waiting.
 * Storing this descriptor does not create a thread or perform a wait. */
wl_err_t wl_endpoint_set_waiter(wl_endpoint_t *endpoint, const wl_waiter_t *waiter);
const wl_waiter_t *wl_endpoint_waiter(const wl_endpoint_t *endpoint);
/* Setup-only borrowed executor binding, immutable until callers have joined.
 * close deliberately retains it: late proxy calls are rejected by the stopped
 * executor without touching mutable endpoint state. Executor storage must
 * outlive its bound endpoint and all callers. init clears an old binding. */
wl_err_t wl_endpoint_set_rpc_executor(wl_endpoint_t *endpoint,
    const struct wl_rpc_executor *executor);
const struct wl_rpc_executor *wl_endpoint_rpc_executor(const wl_endpoint_t *endpoint);
wl_err_t wl_endpoint_get_clock(const wl_endpoint_t *endpoint, wl_clock_t *clock);
wl_err_t wl_endpoint_step(wl_endpoint_t *endpoint, size_t event_budget);
wl_err_t wl_endpoint_get_hint(const wl_endpoint_t *endpoint,
                             wl_poll_hint_t *hint);
/* Assembly bridge, not a business operation. Inside a step returns the pass
 * snapshot; outside a step reads the configured clock once. Does not poll. */
wl_err_t wl_endpoint_now(const wl_endpoint_t *endpoint, wl_time_ms_t *now_ms);
const wl_pump_result_t *wl_endpoint_last_step(const wl_endpoint_t *endpoint);
/* Stops an attached adapter before invalidating the endpoint. Idempotent.
 * Close every endpoint before releasing a shared adapter's storage. */
void wl_endpoint_close(wl_endpoint_t *endpoint);

#ifdef __cplusplus
}
#endif
#endif

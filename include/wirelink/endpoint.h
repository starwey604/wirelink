/* SPDX-License-Identifier: Apache-2.0 */
#ifndef INCLUDE_WIRELINK_ENDPOINT_H_
#define INCLUDE_WIRELINK_ENDPOINT_H_

#include "wirelink/pump.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Default owner-side assembly, normally embedded by WLC. Zero-initialize
 * before first init; never copy/move while initialized. Members are private.
 * This object creates no thread and owns no transport or external buffers. */
typedef struct wl_endpoint {
  wl_ctx_t private_link;
  wl_pump_hooks_t private_hooks;
  wl_pump_result_t private_step;
  uint8_t private_ready;
} wl_endpoint_t;

wl_err_t wl_endpoint_init(wl_endpoint_t *endpoint, const wl_config_t *config,
                         const wl_storage_t *storage,
                         const wl_pump_hooks_t *application);
wl_ctx_t *wl_endpoint_link(wl_endpoint_t *endpoint);
uint8_t wl_endpoint_has_adapter(const wl_endpoint_t *endpoint);
/* Adapter integration: only adapter fields are installed. Application hooks
 * remain owned by the generated assembly. Attach before driving the owner. */
wl_err_t wl_endpoint_attach(wl_endpoint_t *endpoint,
                           const wl_pump_hooks_t *adapter);
wl_err_t wl_endpoint_step(wl_endpoint_t *endpoint, wl_time_ms_t now_ms,
                         size_t event_budget);
wl_err_t wl_endpoint_get_hint(const wl_endpoint_t *endpoint,
                             wl_time_ms_t now_ms, wl_poll_hint_t *hint);
const wl_pump_result_t *wl_endpoint_last_step(const wl_endpoint_t *endpoint);
/* Stops an attached adapter before invalidating the endpoint. Idempotent.
 * Close every endpoint before releasing a shared adapter's storage. */
void wl_endpoint_close(wl_endpoint_t *endpoint);

#ifdef __cplusplus
}
#endif
#endif

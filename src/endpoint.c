/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/endpoint.h"

#include <string.h>

wl_err_t wl_endpoint_init(wl_endpoint_t *endpoint, const wl_config_t *config,
                         const wl_storage_t *storage,
                         const wl_clock_t *clock,
                         const wl_pump_hooks_t *application) {
  int result;
  if (endpoint == NULL) return WL_ERR_INVALID_ARG;
  if (endpoint->private_ready != 0U) return WL_ERR_INVALID_STATE;
  if (clock == NULL || clock->now_ms == NULL) return WL_ERR_INVALID_ARG;
  result = wl_init(&endpoint->private_link, config, storage);
  if (result != WL_OK) return result;
  memset(&endpoint->private_hooks, 0, sizeof(endpoint->private_hooks));
  memset(&endpoint->private_step, 0, sizeof(endpoint->private_step));
  endpoint->private_clock = *clock;
  memset(&endpoint->private_waiter, 0, sizeof(endpoint->private_waiter));
  endpoint->private_executor = NULL;
  endpoint->private_now = 0U;
  endpoint->private_stepping = 0U;
  if (application != NULL) {
    endpoint->private_hooks.application_user_data = application->application_user_data;
    endpoint->private_hooks.application_progress = application->application_progress;
    endpoint->private_hooks.application_deadline_hint = application->application_deadline_hint;
    endpoint->private_hooks.on_event = application->on_event;
  }
  endpoint->private_ready = 1U;
  return WL_OK;
}

wl_ctx_t *wl_endpoint_link(wl_endpoint_t *endpoint) {
  return endpoint != NULL && endpoint->private_ready != 0U
             ? &endpoint->private_link : NULL;
}

uint8_t wl_endpoint_has_adapter(const wl_endpoint_t *endpoint) {
  return endpoint != NULL && (endpoint->private_hooks.service != NULL ||
      endpoint->private_hooks.quiesce != NULL ||
      endpoint->private_hooks.adapter_user_data != NULL ||
      endpoint->private_hooks.adapter_deadline_hint != NULL);
}

wl_err_t wl_endpoint_attach(wl_endpoint_t *endpoint,
                           const wl_pump_hooks_t *adapter) {
  if (endpoint == NULL || adapter == NULL) return WL_ERR_INVALID_ARG;
  if (endpoint->private_ready == 0U) return WL_ERR_NOT_INITIALIZED;
  if (wl_endpoint_has_adapter(endpoint)) return WL_ERR_BUSY;
  endpoint->private_hooks.adapter_user_data = adapter->adapter_user_data;
  endpoint->private_hooks.service = adapter->service;
  endpoint->private_hooks.quiesce = adapter->quiesce;
  endpoint->private_hooks.adapter_deadline_hint = adapter->adapter_deadline_hint;
  return WL_OK;
}

wl_err_t wl_endpoint_set_waiter(wl_endpoint_t *endpoint, const wl_waiter_t *waiter) {
  if (endpoint == NULL) return WL_ERR_INVALID_ARG;
  if (!endpoint->private_ready) return WL_ERR_NOT_INITIALIZED;
  if (endpoint->private_stepping) return WL_ERR_REENTRANT;
  if (waiter != NULL && waiter->wait == NULL) return WL_ERR_INVALID_ARG;
  if (waiter != NULL) endpoint->private_waiter = *waiter;
  else memset(&endpoint->private_waiter, 0, sizeof(endpoint->private_waiter));
  return WL_OK;
}

const wl_waiter_t *wl_endpoint_waiter(const wl_endpoint_t *endpoint) {
  return endpoint != NULL && endpoint->private_ready && endpoint->private_waiter.wait != NULL
      ? &endpoint->private_waiter : NULL;
}

wl_err_t wl_endpoint_set_rpc_executor(wl_endpoint_t *endpoint,
    const struct wl_rpc_executor *executor) {
  if (endpoint == NULL) return WL_ERR_INVALID_ARG;
  if (!endpoint->private_ready) return WL_ERR_NOT_INITIALIZED;
  if (endpoint->private_stepping) return WL_ERR_REENTRANT;
  endpoint->private_executor = executor;
  return WL_OK;
}

const struct wl_rpc_executor *wl_endpoint_rpc_executor(const wl_endpoint_t *endpoint) {
  return endpoint != NULL ? endpoint->private_executor : NULL;
}

wl_err_t wl_endpoint_get_clock(const wl_endpoint_t *endpoint, wl_clock_t *clock) {
  if (endpoint == NULL || clock == NULL) return WL_ERR_INVALID_ARG;
  if (!endpoint->private_ready) return WL_ERR_NOT_INITIALIZED;
  *clock = endpoint->private_clock;
  return WL_OK;
}

wl_err_t wl_endpoint_now(const wl_endpoint_t *endpoint, wl_time_ms_t *now_ms) {
  if (endpoint == NULL || now_ms == NULL) return WL_ERR_INVALID_ARG;
  if (endpoint->private_ready == 0U) return WL_ERR_NOT_INITIALIZED;
  *now_ms = endpoint->private_stepping != 0U ? endpoint->private_now
      : endpoint->private_clock.now_ms(endpoint->private_clock.user_data);
  return WL_OK;
}

wl_err_t wl_endpoint_step(wl_endpoint_t *endpoint, size_t event_budget) {
  int result;
  if (endpoint == NULL) return WL_ERR_INVALID_ARG;
  if (endpoint->private_ready == 0U) return WL_ERR_NOT_INITIALIZED;
  if (event_budget == 0U) return WL_ERR_INVALID_ARG;
  if (endpoint->private_stepping != 0U) return WL_ERR_REENTRANT;
  endpoint->private_now = endpoint->private_clock.now_ms(endpoint->private_clock.user_data);
  endpoint->private_stepping = 1U;
  result = wl_pump_step(&endpoint->private_link, endpoint->private_now, event_budget,
                       &endpoint->private_hooks, &endpoint->private_step);
  endpoint->private_stepping = 0U;
  if (result != WL_OK) return result;
  if (endpoint->private_step.service_errors != 0U) return endpoint->private_step.service_result;
  if (endpoint->private_step.poll_errors != 0U) return endpoint->private_step.poll_result;
  return WL_OK;
}

wl_err_t wl_endpoint_get_hint(const wl_endpoint_t *endpoint,
                             wl_poll_hint_t *hint) {
  wl_time_ms_t now_ms;
  int result;
  if (hint == NULL) return WL_ERR_INVALID_ARG;
  result = wl_endpoint_now(endpoint, &now_ms);
  if (result != WL_OK) return result;
  return wl_pump_get_hint(&endpoint->private_link, now_ms,
                          &endpoint->private_hooks, hint);
}

const wl_pump_result_t *wl_endpoint_last_step(const wl_endpoint_t *endpoint) {
  return endpoint != NULL ? &endpoint->private_step : NULL;
}

void wl_endpoint_close(wl_endpoint_t *endpoint) {
  if (endpoint == NULL || endpoint->private_ready == 0U) return;
  wl_pump_quiesce(&endpoint->private_hooks);
  memset(&endpoint->private_hooks, 0, sizeof(endpoint->private_hooks));
  memset(&endpoint->private_clock, 0, sizeof(endpoint->private_clock));
  memset(&endpoint->private_waiter, 0, sizeof(endpoint->private_waiter));
  endpoint->private_ready = 0U;
}

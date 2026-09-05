/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/endpoint.h"

#include <string.h>

wl_err_t wl_endpoint_init(wl_endpoint_t *endpoint, const wl_config_t *config,
                         const wl_storage_t *storage,
                         const wl_pump_hooks_t *application) {
  int result;
  if (endpoint == NULL) return WL_ERR_INVALID_ARG;
  if (endpoint->private_ready != 0U) return WL_ERR_INVALID_STATE;
  result = wl_init(&endpoint->private_link, config, storage);
  if (result != WL_OK) return result;
  memset(&endpoint->private_hooks, 0, sizeof(endpoint->private_hooks));
  memset(&endpoint->private_step, 0, sizeof(endpoint->private_step));
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

wl_err_t wl_endpoint_step(wl_endpoint_t *endpoint, wl_time_ms_t now_ms,
                         size_t event_budget) {
  int result;
  if (endpoint == NULL) return WL_ERR_INVALID_ARG;
  if (endpoint->private_ready == 0U) return WL_ERR_NOT_INITIALIZED;
  result = wl_pump_step(&endpoint->private_link, now_ms, event_budget,
                       &endpoint->private_hooks, &endpoint->private_step);
  if (result != WL_OK) return result;
  if (endpoint->private_step.service_errors != 0U) return endpoint->private_step.service_result;
  if (endpoint->private_step.poll_errors != 0U) return endpoint->private_step.poll_result;
  return WL_OK;
}

wl_err_t wl_endpoint_get_hint(const wl_endpoint_t *endpoint,
                             wl_time_ms_t now_ms, wl_poll_hint_t *hint) {
  if (endpoint == NULL) return WL_ERR_INVALID_ARG;
  if (endpoint->private_ready == 0U) return WL_ERR_NOT_INITIALIZED;
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
  endpoint->private_ready = 0U;
}

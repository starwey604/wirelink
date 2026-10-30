/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/endpoint.h"
#include "wirelink/port.h"

#include <string.h>

static wl_pump_event_disposition_t endpoint_event(void *context, wl_ctx_t *link,
    const wl_event_t *event, wl_time_ms_t now_ms) {
  wl_endpoint_t *endpoint = context;
  endpoint->private_policy_pending = endpoint->private_policy.step != NULL;
  for (size_t i = 0U; i < endpoint->private_service_count; ++i)
    if (endpoint->private_services[i].step != NULL)
      endpoint->private_services_active |= UINT64_C(1) << i;
  if ((event->type == WL_EVT_RELIABLE_RX ||
       event->type == WL_EVT_UNRELIABLE_RX) && event->peer_session_id != 0U &&
      event->peer_session_id != endpoint->private_peer_session) {
    uint64_t previous = endpoint->private_peer_session;
    endpoint->private_peer_session = event->peer_session_id;
    for (size_t i = 0U; i < endpoint->private_service_count; ++i) {
      const wl_endpoint_service_t *service = &endpoint->private_services[i];
      if (service->on_peer_session != NULL)
        service->on_peer_session(service->user_data, link, previous,
                                 event->peer_session_id, now_ms);
    }
  }
  if (endpoint->private_policy.on_event != NULL)
    endpoint->private_policy.on_event(endpoint->private_policy.user_data,
                                       link, event, now_ms);
  return endpoint->private_hooks.on_event != NULL
      ? endpoint->private_hooks.on_event(endpoint->private_hooks.application_user_data,
                                         link, event, now_ms)
      : WL_PUMP_EVENT_UNHANDLED;
}

static uint8_t endpoint_step_application(void *context, wl_ctx_t *link,
                                         wl_time_ms_t now_ms) {
  wl_endpoint_t *endpoint = context;
  uint8_t did_work = 0U;
  /* The pump already checked aggregate readiness. With only the application
   * present, repeating that same query adds a second RPC timer scan. */
  if (endpoint->private_policy.step == NULL &&
      endpoint->private_policy.deadline_hint == NULL &&
      endpoint->private_service_count == 0U) {
    return endpoint->private_hooks.application_step != NULL
        ? endpoint->private_hooks.application_step(
            endpoint->private_hooks.application_user_data, link, now_ms)
        : 0U;
  }
  uint32_t generation = wl_link_resource_generation(link);
  if (generation != endpoint->private_resource_generation) {
    endpoint->private_resource_generation = generation;
    endpoint->private_policy_pending |= endpoint->private_policy.step != NULL;
    for (size_t i = 0U; i < endpoint->private_service_count; ++i)
      if (endpoint->private_services[i].step != NULL)
        endpoint->private_services_active |= UINT64_C(1) << i;
  }
  if (endpoint->private_policy.step != NULL &&
      (endpoint->private_policy_pending ||
       (endpoint->private_policy.deadline_hint != NULL &&
        endpoint->private_policy.deadline_hint(endpoint->private_policy.user_data, now_ms) == 0U))) {
    endpoint->private_policy_pending = 0U;
    wl_work_result_t work = endpoint->private_policy.step(
        endpoint->private_policy.user_data, link, now_ms);
    endpoint->private_policy_pending |= work.runnable;
    did_work |= work.did_work;
  }
  if (endpoint->private_hooks.application_step != NULL &&
      endpoint->private_hooks.application_readiness != NULL &&
      endpoint->private_hooks.application_readiness(
          endpoint->private_hooks.application_user_data, link, now_ms).runnable)
    did_work |= endpoint->private_hooks.application_step(
        endpoint->private_hooks.application_user_data, link, now_ms);
  size_t index = endpoint->private_service_cursor;
  for (size_t i = 0U; i < endpoint->private_service_count; ++i) {
    const wl_endpoint_service_t *service = &endpoint->private_services[index];
    const uint64_t bit = UINT64_C(1) << index;
    if (service->step != NULL && ((endpoint->private_services_active & bit) ||
        (service->deadline_hint != NULL &&
         service->deadline_hint(service->user_data, now_ms) == 0U))) {
      endpoint->private_services_active &= ~bit;
      wl_work_result_t work = service->step(service->user_data, link, now_ms);
      if (work.runnable) endpoint->private_services_active |= bit;
      did_work |= work.did_work;
    }
    if (++index == endpoint->private_service_count) index = 0U;
  }
  if (endpoint->private_service_count != 0U &&
      ++endpoint->private_service_cursor == endpoint->private_service_count)
    endpoint->private_service_cursor = 0U;
  return did_work;
}

static wl_readiness_t endpoint_readiness(const void *context,
    const wl_ctx_t *link, wl_time_ms_t now_ms) {
  const wl_endpoint_t *endpoint = context;
  wl_readiness_t value = wl_readiness_idle();
  if (endpoint->private_hooks.application_readiness != NULL)
    wl_readiness_merge(&value, endpoint->private_hooks.application_readiness(
        endpoint->private_hooks.application_user_data, link, now_ms));
  if (endpoint->private_policy.deadline_hint != NULL)
    wl_readiness_merge(&value, wl_readiness_at(endpoint->private_policy.deadline_hint(
        endpoint->private_policy.user_data, now_ms)));
  for (size_t i = 0U; i < endpoint->private_service_count; ++i) {
    const wl_endpoint_service_t *service = &endpoint->private_services[i];
    if (service->deadline_hint != NULL)
      wl_readiness_merge(&value, wl_readiness_at(service->deadline_hint(
          service->user_data, now_ms)));
  }
  value.runnable |= endpoint->private_policy_pending || endpoint->private_services_active;
  if ((endpoint->private_policy.step != NULL || endpoint->private_service_count != 0U) &&
      endpoint->private_resource_generation != wl_link_resource_generation(link))
    value.runnable = 1U;
  return value;
}

static wl_pump_hooks_t endpoint_hooks(const wl_endpoint_t *endpoint) {
  wl_pump_hooks_t hooks = endpoint->private_hooks;
  if (endpoint->private_service_count != 0U ||
      endpoint->private_policy.on_event != NULL ||
      endpoint->private_policy.step != NULL ||
      endpoint->private_policy.deadline_hint != NULL) {
    hooks.application_user_data = (void *)endpoint;
    hooks.on_event = endpoint_event;
    hooks.application_step = endpoint_step_application;
    hooks.application_readiness = endpoint_readiness;
  }
  return hooks;
}

wl_err_t wl_endpoint_init(wl_endpoint_t *endpoint, const wl_config_t *config,
                         const wl_storage_t *storage,
                         const wl_clock_t *clock,
                         const wl_pump_hooks_t *application) {
  int result;
  if (endpoint == NULL || (application != NULL &&
      application->application_step != NULL && application->application_readiness == NULL))
    return WL_ERR_INVALID_ARG;
  if (endpoint->private_ready != 0U) return WL_ERR_INVALID_STATE;
  if (clock == NULL || clock->now_ms == NULL) return WL_ERR_INVALID_ARG;
  result = wl_init(&endpoint->private_link, config, storage);
  if (result != WL_OK) return result;
  memset(&endpoint->private_hooks, 0, sizeof(endpoint->private_hooks));
  memset(&endpoint->private_policy, 0, sizeof(endpoint->private_policy));
  memset(&endpoint->private_step, 0, sizeof(endpoint->private_step));
  endpoint->private_clock = *clock;
  memset(&endpoint->private_waiter, 0, sizeof(endpoint->private_waiter));
  endpoint->private_executor = NULL;
  endpoint->private_now = 0U;
  endpoint->private_stepping = 0U;
  endpoint->private_policy_pending = 0U;
  endpoint->private_services = NULL;
  endpoint->private_service_count = 0U;
  endpoint->private_service_cursor = 0U;
  endpoint->private_services_active = 0U;
  endpoint->private_resource_generation = wl_link_resource_generation(&endpoint->private_link);
  endpoint->private_peer_session = 0U;
  endpoint->private_started = 0U;
  if (application != NULL) {
    endpoint->private_hooks.application_user_data = application->application_user_data;
    endpoint->private_hooks.application_step = application->application_step;
    endpoint->private_hooks.application_readiness = application->application_readiness;
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
      endpoint->private_hooks.adapter_readiness != NULL);
}

wl_err_t wl_endpoint_attach(wl_endpoint_t *endpoint,
                           const wl_pump_hooks_t *adapter) {
  if (endpoint == NULL || adapter == NULL ||
      (adapter->service != NULL && adapter->adapter_readiness == NULL))
    return WL_ERR_INVALID_ARG;
  if (endpoint->private_ready == 0U) return WL_ERR_NOT_INITIALIZED;
  if (wl_endpoint_has_adapter(endpoint)) return WL_ERR_BUSY;
  endpoint->private_hooks.adapter_user_data = adapter->adapter_user_data;
  endpoint->private_hooks.service = adapter->service;
  endpoint->private_hooks.quiesce = adapter->quiesce;
  endpoint->private_hooks.adapter_readiness = adapter->adapter_readiness;
  return WL_OK;
}

wl_err_t wl_endpoint_set_services(wl_endpoint_t *endpoint,
    const wl_endpoint_service_t *services, size_t count) {
  if (endpoint == NULL || (services == NULL && count != 0U) || count > 64U)
    return WL_ERR_INVALID_ARG;
  if (!endpoint->private_ready) return WL_ERR_NOT_INITIALIZED;
  if (endpoint->private_stepping) return WL_ERR_REENTRANT;
  if (endpoint->private_started) return WL_ERR_INVALID_STATE;
  endpoint->private_services = services;
  endpoint->private_service_count = count;
  endpoint->private_service_cursor = 0U;
  endpoint->private_services_active = 0U;
  for (size_t i = 0U; i < count; ++i)
    if (services[i].step != NULL) endpoint->private_services_active |= UINT64_C(1) << i;
  return WL_OK;
}

wl_err_t wl_endpoint_activate_service(wl_endpoint_t *endpoint, size_t index) {
  if (endpoint == NULL || index >= endpoint->private_service_count)
    return WL_ERR_INVALID_ARG;
  if (!endpoint->private_ready) return WL_ERR_NOT_INITIALIZED;
  endpoint->private_services_active |= UINT64_C(1) << index;
  if (endpoint->private_waiter.notify != NULL)
    endpoint->private_waiter.notify(endpoint->private_waiter.user_data);
  return WL_OK;
}

wl_err_t wl_endpoint_activate_policy(wl_endpoint_t *endpoint) {
  if (endpoint == NULL) return WL_ERR_INVALID_ARG;
  if (!endpoint->private_ready) return WL_ERR_NOT_INITIALIZED;
  endpoint->private_policy_pending = endpoint->private_policy.step != NULL;
  if (endpoint->private_waiter.notify != NULL)
    endpoint->private_waiter.notify(endpoint->private_waiter.user_data);
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

wl_err_t wl_endpoint_set_policy(wl_endpoint_t *endpoint,
                                const wl_endpoint_policy_t *policy) {
  if (endpoint == NULL) return WL_ERR_INVALID_ARG;
  if (!endpoint->private_ready) return WL_ERR_NOT_INITIALIZED;
  if (endpoint->private_stepping) return WL_ERR_REENTRANT;
  if (policy != NULL) endpoint->private_policy = *policy;
  else memset(&endpoint->private_policy, 0, sizeof(endpoint->private_policy));
  endpoint->private_policy_pending = endpoint->private_policy.step != NULL;
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
  wl_pump_hooks_t hooks;
  if (endpoint == NULL) return WL_ERR_INVALID_ARG;
  if (endpoint->private_ready == 0U) return WL_ERR_NOT_INITIALIZED;
  if (event_budget == 0U) return WL_ERR_INVALID_ARG;
  if (endpoint->private_stepping != 0U) return WL_ERR_REENTRANT;
  endpoint->private_now = endpoint->private_clock.now_ms(endpoint->private_clock.user_data);
  endpoint->private_stepping = 1U;
  endpoint->private_started = 1U;
  hooks = endpoint_hooks(endpoint);
  result = wl_pump_step(&endpoint->private_link, endpoint->private_now, event_budget,
                       &hooks, &endpoint->private_step);
  endpoint->private_stepping = 0U;
  if (result != WL_OK) return result;
  if (endpoint->private_step.service_errors != 0U) return endpoint->private_step.service_result;
  if (endpoint->private_step.poll_errors != 0U) return endpoint->private_step.poll_result;
  return WL_OK;
}

wl_err_t wl_endpoint_get_readiness(const wl_endpoint_t *endpoint,
                             wl_readiness_t *hint) {
  wl_time_ms_t now_ms;
  int result;
  if (hint == NULL) return WL_ERR_INVALID_ARG;
  result = wl_endpoint_now(endpoint, &now_ms);
  if (result != WL_OK) return result;
  return wl_endpoint_get_readiness_at(endpoint, now_ms, hint);
}

const wl_pump_result_t *wl_endpoint_last_step(const wl_endpoint_t *endpoint) {
  return endpoint != NULL ? &endpoint->private_step : NULL;
}

wl_err_t wl_endpoint_get_readiness_at(const wl_endpoint_t *endpoint,
    wl_time_ms_t now_ms, wl_readiness_t *hint) {
  if (endpoint == NULL || hint == NULL) return WL_ERR_INVALID_ARG;
  if (!endpoint->private_ready) return WL_ERR_NOT_INITIALIZED;
  wl_pump_hooks_t hooks = endpoint_hooks(endpoint);
  return wl_pump_get_readiness(&endpoint->private_link, now_ms, &hooks, hint);
}

void wl_endpoint_close(wl_endpoint_t *endpoint) {
  if (endpoint == NULL || endpoint->private_ready == 0U) return;
  if (endpoint->private_stepping != 0U) return;
  endpoint->private_stepping = 1U;
  wl_pump_quiesce(&endpoint->private_hooks);
  for (size_t i = endpoint->private_service_count; i != 0U; --i) {
    const wl_endpoint_service_t *service = &endpoint->private_services[i - 1U];
    if (service->on_close != NULL)
      service->on_close(service->user_data, &endpoint->private_link);
  }
  endpoint->private_services = NULL;
  endpoint->private_service_count = 0U;
  memset(&endpoint->private_hooks, 0, sizeof(endpoint->private_hooks));
  memset(&endpoint->private_policy, 0, sizeof(endpoint->private_policy));
  memset(&endpoint->private_clock, 0, sizeof(endpoint->private_clock));
  memset(&endpoint->private_waiter, 0, sizeof(endpoint->private_waiter));
  endpoint->private_ready = 0U;
  endpoint->private_stepping = 0U;
}

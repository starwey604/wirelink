/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "quickstart_runtime.h"
#include "wirelink/loopback.h"

#define APP_MAX_PAYLOAD 64U
#define APP_UNIT_CAPACITY 96U

typedef struct {
  wl_ctx_t link;
  uint8_t tx_payload[APP_MAX_PAYLOAD];
  uint8_t tx_unit[APP_UNIT_CAPACITY];
  uint8_t control_unit[APP_UNIT_CAPACITY];
  uint8_t rx_fallback[APP_UNIT_CAPACITY];
} endpoint_t;

typedef struct {
  quickstart_runtime_instance_t instance;
  quickstart_runtime_default_storage_t arena;
  quickstart_runtime_pump_t pump;
  quickstart_runtime_result_t last_result;
} application_runtime_t;

typedef struct {
  wl_rpc_server_request_t request;
  int32_t left;
  int32_t right;
  uint32_t calls;
} add_server_t;

static int endpoint_init(endpoint_t *endpoint, uint64_t session_id) {
  const wl_config_t config = {
      .max_payload_len = APP_MAX_PAYLOAD,
      .envelope = WL_ENVELOPE_NATIVE_PACKET,
      .integrity = WL_INTEGRITY_CRC32C,
      .session_id = session_id,
      .max_retries = 2U,
      .ack_timeout_ms = 20U,
      .max_transmission_unit = APP_UNIT_CAPACITY,
  };
  const wl_storage_t storage = {
      .tx_payload = endpoint->tx_payload,
      .tx_payload_size = sizeof(endpoint->tx_payload),
      .tx_unit = endpoint->tx_unit,
      .tx_unit_size = sizeof(endpoint->tx_unit),
      .control_unit = endpoint->control_unit,
      .control_unit_size = sizeof(endpoint->control_unit),
      .rx_fallback = endpoint->rx_fallback,
      .rx_fallback_size = sizeof(endpoint->rx_fallback),
  };
  int result;

  memset(endpoint, 0, sizeof(*endpoint));
  result = wl_init(&endpoint->link, &config, &storage);
  return result;
}

static void observe_runtime_result(
    void *user_data, const quickstart_runtime_result_t *result) {
  application_runtime_t *runtime = user_data;

  runtime->last_result = *result;
}

static int runtime_init(application_runtime_t *runtime,
                        const quickstart_runtime_config_t *config) {
  quickstart_runtime_storage_t storage;
  int result;

  memset(runtime, 0, sizeof(*runtime));
  storage = quickstart_runtime_default_storage_descriptor(&runtime->arena);
  result = quickstart_runtime_init(&runtime->instance, config, &storage);
  if (result != WL_OK) {
    return result;
  }
  return quickstart_runtime_pump_init(
      &runtime->pump, &runtime->instance.runtime, observe_runtime_result,
      runtime);
}

static int deliver(wl_loopback_t *loopback) {
  wl_loopback_service_result_t service;
  int result = wl_loopback_service(loopback, 1U, &service);

  if (result != WL_OK) {
    return result;
  }
  return service.delivered == 1U ? WL_OK : WL_ERR_NO_DATA;
}

static int poll_dispatch(endpoint_t *endpoint, application_runtime_t *runtime,
                         wl_time_ms_t now_ms,
                         quickstart_runtime_result_t *out_result) {
  const wl_pump_hooks_t hooks =
      quickstart_runtime_pump_hooks(&runtime->pump);
  wl_pump_result_t step;
  int result;

  memset(&runtime->last_result, 0, sizeof(runtime->last_result));
  /* Handle-less local completions do not cross the application boundary. */
  runtime->last_result.domain = QUICKSTART_RUNTIME_NON_RX;
  result = wl_pump_step(&endpoint->link, now_ms, 1U, &hooks, &step);
  if (result != WL_OK) {
    return result;
  }
  if (step.events != 1U) {
    return WL_ERR_NO_DATA;
  }
  *out_result = runtime->last_result;
  return WL_OK;
}

static int32_t handle_add(void *user_data, const add_request_t *request,
                          const wl_rpc_server_request_t *server_request,
                          wl_delivery_t delivery) {
  add_server_t *server = user_data;

  if (request == NULL || server_request == NULL ||
      delivery != WL_DELIVERY_RELIABLE || !request->has_left ||
      !request->has_right) {
    return -1;
  }
  server->request = *server_request;
  server->left = request->left;
  server->right = request->right;
  ++server->calls;
  return 0;
}

static int run_unreliable(endpoint_t *device, endpoint_t *controller,
                          application_runtime_t *device_runtime,
                          application_runtime_t *controller_runtime,
                          wl_loopback_t *loopback) {
  telemetry_t telemetry;
  quickstart_send_result_t sent;
  quickstart_runtime_result_t dispatched;
  quickstart_telemetry_latest_view_t view;

  telemetry_clear(&telemetry);
  telemetry.has_sample = true;
  telemetry.sample = 7U;
  telemetry.has_temperature_centi_c = true;
  telemetry.temperature_centi_c = 2350;
  sent = quickstart_telemetry_send(&device->link, &telemetry,
                                   WL_DELIVERY_UNRELIABLE);
  if (sent.domain != QUICKSTART_SEND_OK ||
      deliver(loopback) != WL_OK ||
      poll_dispatch(controller, controller_runtime, 1U, &dispatched) != WL_OK ||
      dispatched.domain != QUICKSTART_RUNTIME_OK ||
      quickstart_telemetry_latest_acquire(&controller_runtime->instance.runtime,
                                          &view) != WL_OK ||
      view.value == NULL || view.value->sample != 7U ||
      view.value->temperature_centi_c != 2350 ||
      quickstart_telemetry_latest_release(&controller_runtime->instance.runtime,
                                          &view) != WL_OK) {
    return 1;
  }

  /* Drain the sender's handle-less local completion. */
  if (poll_dispatch(device, device_runtime, 1U, &dispatched) != WL_OK ||
      dispatched.domain != QUICKSTART_RUNTIME_NON_RX) {
    return 2;
  }
  return 0;
}

static int run_rpc(endpoint_t *controller, endpoint_t *device,
                   application_runtime_t *controller_runtime,
                   application_runtime_t *device_runtime,
                   add_server_t *server, wl_loopback_t *loopback) {
  add_request_t request;
  add_response_t response;
  add_response_t decoded;
  quickstart_runtime_result_t result;
  const quickstart_runtime_rpc_detail_t *rpc_detail;
  wl_pump_result_t step;
  wl_pump_hooks_t hooks;
  wl_rpc_client_result_t client;
  uint32_t operation_id;

  add_request_clear(&request);
  request.has_left = true;
  request.left = 20;
  request.has_right = true;
  request.right = 22;
  result = quickstart_add_client_start(
      &controller->link, &controller_runtime->instance.runtime, &request, 100U,
      10U);
  rpc_detail = quickstart_runtime_result_rpc_detail(&result);
  if (!quickstart_runtime_result_ok(&result) || rpc_detail == NULL ||
      rpc_detail->operation_id == 0U) {
    return 1;
  }
  operation_id = rpc_detail->operation_id;

  /* Request, request ACK, and the client's terminal TX event. */
  if (deliver(loopback) != WL_OK ||
      poll_dispatch(device, device_runtime, 11U, &result) != WL_OK ||
      !quickstart_runtime_result_ok(&result) || server->calls != 1U ||
      deliver(loopback) != WL_OK ||
      poll_dispatch(controller, controller_runtime, 12U, &result) != WL_OK ||
      !quickstart_runtime_result_ok(&result)) {
    return 2;
  }

  add_response_clear(&response);
  response.has_sum = true;
  response.sum = server->left + server->right;
  result = quickstart_add_server_complete(
      &device_runtime->instance.runtime, &server->request, &response, 13U);
  hooks = quickstart_runtime_pump_hooks(&device_runtime->pump);
  if (!quickstart_runtime_result_ok(&result) ||
      wl_pump_step(&device->link, 13U, 1U, &hooks, &step) != WL_OK ||
      device_runtime->pump.last_service_result != WL_RPC_OK ||
      device_runtime->pump.last_service.responses_submitted != 1U) {
    return 3;
  }

  /* Response, response ACK, and the server's terminal TX event. */
  if (deliver(loopback) != WL_OK ||
      poll_dispatch(controller, controller_runtime, 14U, &result) != WL_OK ||
      !quickstart_runtime_result_ok(&result) ||
      deliver(loopback) != WL_OK ||
      poll_dispatch(device, device_runtime, 15U, &result) != WL_OK ||
      !quickstart_runtime_result_ok(&result)) {
    return 4;
  }

  memset(&client, 0, sizeof(client));
  add_response_clear(&decoded);
  if (quickstart_add_client_inspect(&controller_runtime->instance.runtime,
                                    operation_id, &client) != WL_RPC_OK ||
      client.state != WL_RPC_CLIENT_COMPLETED) {
    return 5;
  }
  result = quickstart_add_client_decode(&client, &decoded);
  if (!quickstart_runtime_result_ok(&result) || !decoded.has_sum ||
      decoded.sum != 42 || quickstart_add_client_release(
                               &controller_runtime->instance.runtime,
                               operation_id) != WL_RPC_OK) {
    return 6;
  }
  return 0;
}

int main(void) {
  endpoint_t controller;
  endpoint_t device;
  application_runtime_t controller_runtime;
  application_runtime_t device_runtime;
  wl_loopback_t loopback;
  add_server_t server = {0};
  quickstart_runtime_config_t controller_config;
  quickstart_runtime_config_t device_config;
  int result;

  result = quickstart_runtime_config_defaults(&controller_config);
  if (result == WL_OK) {
    result = quickstart_runtime_config_enable_client(&controller_config);
  }
  if (result == WL_OK) {
    result = quickstart_runtime_config_defaults(&device_config);
  }
  if (result == WL_OK) {
    result = quickstart_runtime_config_enable_server(&device_config);
  }
  if (result != WL_OK) {
    fprintf(stderr, "runtime configuration failed: %s\n", wl_err_str(result));
    return 1;
  }
  device_config.rpc_server_pending_timeout_ms = 1000U;
  device_config.rpc_server_cache_ttl_ms = 10000U;
  device_config.add_request_handler = handle_add;
  device_config.add_user_data = &server;

  result = endpoint_init(&controller, UINT64_C(0x1001));
  if (result == WL_OK) {
    result = endpoint_init(&device, UINT64_C(0x2002));
  }
  if (result == WL_OK) {
    result = wl_loopback_init(&loopback, &controller.link, &device.link);
  }
  if (result == WL_OK) {
    result = runtime_init(&controller_runtime, &controller_config);
  }
  if (result == WL_OK) {
    result = runtime_init(&device_runtime, &device_config);
  }
  if (result != WL_OK) {
    fprintf(stderr, "initialization failed: %s\n", wl_err_str(result));
    return 1;
  }

  if (run_unreliable(&device, &controller, &device_runtime,
                     &controller_runtime, &loopback) != 0) {
    fputs("unreliable example failed\n", stderr);
    return 2;
  }
  if (run_rpc(&controller, &device, &controller_runtime, &device_runtime,
              &server, &loopback) != 0) {
    fputs("RPC example failed\n", stderr);
    return 3;
  }

  wl_loopback_quiesce(&loopback);
  puts("unreliable telemetry: sample=7 temperature=23.50 C");
  puts("reliable RPC: 20 + 22 = 42");
  return 0;
}

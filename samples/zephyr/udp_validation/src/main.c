/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include <zephyr/ztest.h>
#include "udp_demo_endpoint.h"
#include "wirelink/zephyr/udp.h"
#include "../../../../tests/support/test_environment.h"

static udp_demo_endpoint_t client, server;
WL_ZEPHYR_UDP_DEFINE(client_udp, UDP_DEMO_ENDPOINT_MAX_PAYLOAD, 2);
WL_ZEPHYR_UDP_DEFINE(server_udp, UDP_DEMO_ENDPOINT_MAX_PAYLOAD, 2);
static wl_time_ms_t now;
static bool real_clock;
static unsigned completions, handlers;
static wl_rpc_completion_t completion;
static add_response_value_t saved_response;
static struct k_thread server_thread;
K_THREAD_STACK_DEFINE(server_stack, 4096);
static bool thread_active;
static wl_err_t server_result;

static wl_time_ms_t clock_read(void *context) {
  (void)context;
  return real_clock ? k_uptime_get_32() : now;
}

static int32_t add(void *context, const add_request_value_t *request,
                   add_response_value_t *response) {
  (void)context;
  ++handlers;
  const int64_t sum = (int64_t)request->left + request->right;
  if (sum < INT32_MIN || sum > INT32_MAX) return 1;
  response->has_sum = true;
  response->sum = (int32_t)sum;
  return 0;
}

static void done(void *context, const wl_rpc_completion_t *result,
                  const add_response_value_t *response) {
  (void)context;
  ++completions;
  completion = *result;
  if (response != NULL) saved_response = *response;
}

static add_request_value_t request(int32_t left, int32_t right) {
  add_request_value_t value;
  add_request_value_clear(&value);
  value.has_left = value.has_right = true;
  value.left = left;
  value.right = right;
  return value;
}

static void before(void *unused) {
  (void)unused;
  now = 1000;
  real_clock = false;
  completions = handlers = 0;
  const wl_clock_t clock = {.now_ms = clock_read};
  zassert_ok(udp_demo_endpoint_init(&client, test_environment_id(1, clock)));
  udp_demo_endpoint_config_t config;
  zassert_ok(udp_demo_endpoint_config_defaults(&config, test_environment_id(1, clock)));
  config.on_add = add;
  zassert_ok(udp_demo_endpoint_init_config(&server, &config));
  /* Ports are private to this Zephyr loopback stack, not host OS sockets. */
  const wl_zephyr_udp_config_t client_config = {.peer_address = "127.0.0.1",
    .peer_port = 49101, .local_address = "127.0.0.1", .local_port = 49100};
  const wl_zephyr_udp_config_t server_config = {.peer_address = "127.0.0.1",
    .peer_port = 49100, .local_address = "127.0.0.1", .local_port = 49101};
  zassert_ok(wl_zephyr_udp_open(&client_udp, udp_demo_endpoint_handle(&client), &client_config));
  zassert_ok(wl_zephyr_udp_open(&server_udp, udp_demo_endpoint_handle(&server), &server_config));
}

static void after(void *unused) {
  (void)unused;
  if (thread_active) {
    zassert_ok(wl_zephyr_udp_request_stop(&server_udp));
    zassert_ok(k_thread_join(&server_thread, K_SECONDS(2)));
    thread_active = false;
  }
  zassert_ok(udp_demo_endpoint_close(&client));
  zassert_ok(udp_demo_endpoint_close(&server));
  zassert_ok(wl_zephyr_udp_close(&client_udp));
  zassert_ok(wl_zephyr_udp_close(&server_udp));
}

static void step_pair(void) {
  zassert_ok(udp_demo_endpoint_step(&server));
  zassert_ok(udp_demo_endpoint_step(&client));
  ++now;
}

ZTEST(udp_validation, test_async_rpc_with_continuous_telemetry) {
  add_request_value_t value = request(20, 22);
  /* No preparatory step and no inspect/release obligation. */
  zassert_ok(udp_demo_endpoint_add_async(&client, &value, 500, done, NULL, NULL));
  memset(&value, 0, sizeof(value));
  unsigned telemetry_seen = 0, accepted = 0;
  for (uint32_t i = 1; i <= 100; ++i) {
    telemetry_t sample;
    telemetry_clear(&sample);
    sample.has_sample = true;
    sample.sample = i;
    udp_demo_send_result_t sent = udp_demo_endpoint_send_telemetry(&server, &sample);
    if (sent.domain == UDP_DEMO_SEND_OK) ++accepted;
    else zassert_equal(sent.core_result, WL_ERR_BUSY);
    step_pair();
    const wl_err_t read = udp_demo_endpoint_read_telemetry(&client, &sample);
    if (read == WL_OK) ++telemetry_seen;
    else zassert_equal(read, WL_ERR_NO_DATA);
  }
  zassert_true(accepted > 0 && telemetry_seen > 0);
  zassert_equal(completions, 1);
  zassert_equal(handlers, 1);
  zassert_equal(completion.status, WL_RPC_SUCCESS);
  zassert_equal(saved_response.sum, 42);
  /* Reuse the small fixed call pool repeatedly; no application release calls. */
  for (unsigned call = 0; call < 8; ++call) {
    value = request(1, 2);
    unsigned target = completions + 1;
    zassert_ok(udp_demo_endpoint_add_async(&client, &value, 500, done, NULL, NULL));
    for (unsigned i = 0; i < 100 && completions < target; ++i) step_pair();
    zassert_equal(completions, target);
    zassert_equal(completion.status, WL_RPC_SUCCESS);
    zassert_equal(saved_response.sum, 3);
    for (unsigned i = 0; i < 4; ++i) step_pair();
  }
}

ZTEST(udp_validation, test_rejection_cancel_and_generated_close) {
  add_request_value_t value = request(INT32_MAX, 1);
  zassert_ok(udp_demo_endpoint_add_async(&client, &value, 500, done, NULL, NULL));
  for (unsigned i = 0; i < 100 && completions == 0; ++i) step_pair();
  zassert_equal(completions, 1);
  zassert_equal(completion.status, WL_RPC_REJECTED);
  zassert_equal(completion.rejection, 1);
  for (unsigned i = 0; i < 4; ++i) step_pair();
  value = request(1, 2);
  wl_rpc_call_t call;
  zassert_ok(udp_demo_endpoint_add_async(&client, &value, 500, done, NULL, &call));
  zassert_ok(udp_demo_endpoint_cancel(&client, &call));
  for (unsigned i = 0; i < 10 && completions == 1; ++i) step_pair();
  zassert_equal(completions, 2);
  zassert_equal(completion.status, WL_RPC_CANCELLED);
  for (unsigned i = 0; i < 8; ++i) step_pair();
  zassert_ok(udp_demo_endpoint_add_async(&client, &value, 500, done, NULL, NULL));
  zassert_equal(wl_zephyr_udp_close(&client_udp), WL_ERR_BUSY);
  zassert_ok(udp_demo_endpoint_close(&client));
  zassert_equal(completions, 3);
  zassert_equal(completion.status, WL_RPC_CANCELLED);
  zassert_ok(udp_demo_endpoint_close(&client));
  zassert_equal(completions, 3);
  zassert_equal(wl_zephyr_udp_notify(&client_udp), WL_ERR_CANCELLED);
}

static void server_owner(void *a, void *b, void *c) {
  (void)a; (void)b; (void)c;
  for (;;) {
    server_result = udp_demo_endpoint_step(&server);
    if (server_result != WL_OK) break;
    server_result = wl_zephyr_udp_wait(&server_udp, UINT32_MAX);
    if (server_result != WL_OK && server_result != WL_ERR_NO_DATA) break;
  }
}

ZTEST(udp_validation, test_synchronous_rpc_with_socket_waiter) {
  real_clock = true;
  thread_active = true;
  server_result = WL_OK;
  k_thread_create(&server_thread, server_stack, K_THREAD_STACK_SIZEOF(server_stack),
                   server_owner, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
  add_request_value_t value = request(20, 22);
  add_response_value_t response;
  wl_rpc_completion_t result = udp_demo_endpoint_add_sync(&client, &value, &response, 500);
  zassert_equal(result.status, WL_RPC_SUCCESS);
  zassert_equal(response.sum, 42);
  wl_zephyr_udp_stats_t stats;
  zassert_ok(wl_zephyr_udp_get_stats(&client_udp, &stats));
  zassert_true(stats.wait_calls > 0);
  zassert_ok(wl_zephyr_udp_request_stop(&server_udp));
  zassert_ok(k_thread_join(&server_thread, K_SECONDS(2)));
  thread_active = false;
  zassert_equal(server_result, WL_ERR_CANCELLED);
}

ZTEST(udp_validation, test_absent_peer_times_out_without_periodic_polling) {
  real_clock = true;
  zassert_ok(udp_demo_endpoint_close(&server));
  zassert_ok(wl_zephyr_udp_close(&server_udp));
  add_request_value_t value = request(20, 22);
  add_response_value_t response;
  wl_rpc_completion_t result = udp_demo_endpoint_add_sync(&client, &value, &response, 250);
  zassert_equal(result.status, WL_RPC_TIMED_OUT);
  wl_zephyr_udp_stats_t stats;
  zassert_ok(wl_zephyr_udp_get_stats(&client_udp, &stats));
  zassert_true(stats.wait_calls > 0);
  zassert_equal(stats.common.errors, 0);
  /* 250 ms RPC deadline and 100 ms ACK timer need only a few waits, not
   * a mandatory 1 ms receive poll. This is a logical count, not a CPU gate. */
  zassert_true(stats.wait_calls <= 8);
}

ZTEST_SUITE(udp_validation, NULL, NULL, before, after, NULL);

/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "calculator_runtime.h"
#include "wirelink/loopback.h"

#define CHECK(x) do { if (!(x)) { printk("CLOCK_HIL FAIL line=%u: %s\n", \
  (unsigned)__LINE__, #x); return -1; } } while (0)

static calculator_endpoint_t client, server;
static wl_loopback_t cable;
static uint32_t clock_reads, handled;
static int mode;

static wl_time_ms_t read_clock(void *user) {
  (void)user;
  ++clock_reads;
  return k_uptime_get_32();
}

static int32_t add(void *user, const add_request_t *request,
    const calculator_add_request_token_t *token, wl_delivery_t delivery) {
  (void)user;
  (void)delivery;
  ++handled;
  if (mode == 2) return 0; /* Deferred work tests the client's real deadline. */
  if (mode == 1) return calculator_endpoint_add_reject(&server, token, 7);
  add_response_t response;
  add_response_clear(&response);
  response.has_sum = true;
  response.sum = request->left + request->right;
  return calculator_endpoint_add_complete(&server, token, &response);
}

static int configure(int selected_mode) {
  calculator_endpoint_config_t a, b;
  calculator_endpoint_close(&client);
  calculator_endpoint_close(&server);
  CHECK(calculator_endpoint_config_defaults(&a, 1U) == WL_OK);
  CHECK(calculator_endpoint_config_defaults(&b, 2U) == WL_OK);
  a.clock = b.clock = (wl_clock_t){read_clock, NULL};
  CHECK(calculator_runtime_config_enable_client(&a.runtime) == WL_OK);
  CHECK(calculator_runtime_config_enable_server(&b.runtime) == WL_OK);
  a.link.ack_timeout_ms = b.link.ack_timeout_ms = 20U;
  a.link.max_retries = b.link.max_retries = 2U;
  b.runtime.rpc_server_pending_timeout_ms = 1000U;
  b.runtime.rpc_server_cache_ttl_ms = 10000U;
  b.runtime.add_request_handler = add;
  CHECK(calculator_endpoint_init_config(&client, &a) == WL_OK);
  CHECK(calculator_endpoint_init_config(&server, &b) == WL_OK);
  CHECK(wl_loopback_connect(&cable, calculator_endpoint_handle(&client),
                            calculator_endpoint_handle(&server)) == WL_OK);
  handled = 0U;
  mode = selected_mode;
  return 0;
}

static int transaction(int selected_mode, uint32_t idle_ms) {
  calculator_add_call_t call;
  calculator_add_result_t result;
  add_request_t request;
  CHECK(configure(selected_mode) == 0);
  k_msleep(idle_ms); /* Deliberately no step before submission. */
  add_request_clear(&request);
  request.has_left = request.has_right = true;
  request.left = 20;
  request.right = 22;
  const uint32_t started = k_uptime_get_32();
  uint32_t reads = clock_reads;
  CHECK(calculator_endpoint_add_call(&client, &request, 50U, &call) == WL_RPC_OK);
  CHECK(clock_reads == reads + 1U);
  for (;;) {
    reads = clock_reads;
    CHECK(calculator_endpoint_step(&client) == WL_OK);
    CHECK(calculator_endpoint_step(&server) == WL_OK);
    CHECK(clock_reads == reads + 2U); /* Inline complete/reject reuse the pass. */
    CHECK(calculator_endpoint_add_inspect(&client, &call, &result) == WL_RPC_OK);
    CHECK(clock_reads == reads + 2U);
    if (result.state == WL_RPC_CLIENT_COMPLETED ||
        result.state == WL_RPC_CLIENT_APPLICATION_ERROR ||
        result.state == WL_RPC_CLIENT_TIMED_OUT) break;
    CHECK(k_uptime_get_32() - started < 500U);
    k_msleep(1);
  }
  const uint32_t elapsed = k_uptime_get_32() - started;
  if (selected_mode == 0) CHECK(result.response_valid && result.response.sum == 42);
  if (selected_mode == 1) CHECK(result.state == WL_RPC_CLIENT_APPLICATION_ERROR && result.application_status == 7);
  if (selected_mode == 2) CHECK(result.state == WL_RPC_CLIENT_TIMED_OUT && elapsed >= 50U);
  CHECK(handled == 1U);
  CHECK(calculator_endpoint_add_release(&client, &call) == WL_RPC_OK);
  wl_adapter_stats_t stats;
  CHECK(wl_loopback_get_stats(&cable, WL_LOOPBACK_ENDPOINT_A, &stats) == WL_OK);
  CHECK(stats.tx_units == (selected_mode == 2 ? 1U : 2U)); /* No premature retry. */
  printk("CLOCK_HIL rpc mode=%d idle_ms=%u elapsed_ms=%u client_units=%llu PASS\n",
      selected_mode, idle_ms, elapsed, (unsigned long long)stats.tx_units);
  return 0;
}

static int performance(void) {
  enum { ITERATIONS = 20000 };
  calculator_endpoint_close(&client);
  calculator_endpoint_close(&server);
  CHECK(calculator_endpoint_init(&client, 3U, (wl_clock_t){read_clock, NULL}) == WL_OK);
  const uint32_t reads = clock_reads;
  const uint32_t begin = k_cycle_get_32();
  for (unsigned i = 0; i < ITERATIONS; ++i)
    CHECK(calculator_endpoint_step(&client) == WL_OK);
  const uint32_t cycles = k_cycle_get_32() - begin;
  CHECK(clock_reads == reads + ITERATIONS);
  printk("CLOCK_HIL idle iterations=%u cycles/op=%u ns/op=%llu reads/op=1 endpoint_bytes=%u\n",
      ITERATIONS, cycles / ITERATIONS,
      (unsigned long long)(k_cyc_to_ns_floor64(cycles) / ITERATIONS),
      (unsigned)sizeof(client));
  calculator_endpoint_close(&client);
  return 0;
}

int main(void) {
  printk("CLOCK_HIL ABI=%u native_ms=%u start\n", CALCULATOR_RUNTIME_CODEGEN_ABI_VERSION,
      k_uptime_get_32());
  if (transaction(0, 75U) || transaction(0, 150U) || transaction(1, 75U) ||
      transaction(2, 75U) || performance()) return 1;
  printk("CLOCK_HIL ALL PASS\n");
  return 0;
}

/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "calculator_kernel.h"
#include "loopback_owner.h"
#include "wirelink/zephyr/kernel_timeout.h"
static calculator_kernel_t endpoints[2];
static example_pair_t pair;
static unsigned reads, completed, handled;
static wl_kernel_result_t result;
static int mode;
static uint32_t read_clock(void) { ++reads; return k_uptime_get_32(); }
static int32_t add(void *user, const add_request_t *request, add_response_value_t *response) {
  (void)user; ++handled;
  if (mode == 1) return 7;
  response->has_sum = true; response->sum = request->left + request->right; return 0;
}
static void done(void *user, wl_kernel_result_t value, const add_response_t *response) {
  (void)user; result = value;
  if (value.outcome == WL_KERNEL_SUCCESS) EXAMPLE_REQUIRE(response && response->sum == 42);
  ++completed;
}
static void transaction(int selected, unsigned idle_ms) {
  mode = selected; completed = handled = 0; unsigned unsupported = 0;
  example_pair_init(&pair, &endpoints[0].core, &endpoints[1].core);
  const calculator_kernel_handlers_t handlers = {.on_rpc_add = selected == 2 ? NULL : add};
  for (unsigned side = 0; side < 2; ++side)
    EXAMPLE_REQUIRE(calculator_kernel_init(&endpoints[side], &pair.ports[side], &handlers, NULL, NULL) == WL_OK);
  k_sleep(K_MSEC(idle_ms)); /* No step before submit: owner supplies fresh now. */
  const uint32_t began = read_clock();
  add_request_value_t request = {.has_left = true, .left = 20, .has_right = true, .right = 22};
  wl_kernel_call_handle_t call; unsigned before = reads;
  EXAMPLE_REQUIRE(calculator_kernel_add_start(&endpoints[0], &request, began, 50, done, NULL, &call) == WL_OK);
  EXAMPLE_REQUIRE(reads == before);
  while (!completed) {
    uint32_t now = read_clock(); before = reads;
    for (unsigned side = 0; side < 2; ++side) {
      const wl_err_t error = calculator_kernel_step(&endpoints[side], now);
      EXAMPLE_REQUIRE(error == WL_OK || (selected == 2 && side == 1 && error == WL_ERR_NOT_SUPPORTED));
      if (error == WL_ERR_NOT_SUPPORTED) ++unsupported;
    }
    EXAMPLE_REQUIRE(wl_packet_loopback_service(&pair.cable, 2) == WL_OK);
    EXAMPLE_REQUIRE(reads == before); /* Kernel/codecs have no clock provider. */
    EXAMPLE_REQUIRE(k_uptime_get_32() - began < 1000);
    if (completed || wl_packet_loopback_ready(&pair.cable)) continue;
    bool runnable = false; uint32_t maximum = 10;
    for (unsigned side = 0; side < 2; ++side) {
      int has_deadline = 0; uint32_t deadline = 0;
      runnable |= wl_kernel_ready(&endpoints[side].core, now, &has_deadline, &deadline) != 0;
      if (has_deadline && deadline - now < maximum) maximum = deadline - now;
    }
    uint32_t elapsed = read_clock() - now;
    if (!runnable && maximum > elapsed) k_sleep(wl_kernel_timeout_ms(maximum - elapsed));
  }
  EXAMPLE_REQUIRE(result.outcome == (selected == 0 ? WL_KERNEL_SUCCESS : selected == 1 ? WL_KERNEL_REJECTED : WL_KERNEL_TIMED_OUT));
  if (selected == 1) EXAMPLE_REQUIRE(result.status == 7);
  if (selected == 2) EXAMPLE_REQUIRE(!handled && unsupported == 1 && k_uptime_get_32() - began >= 50);
  example_pair_close(&pair);
  printk("CLOCK_CHECK rpc mode=%u idle_ms=%u elapsed_ms=%u explicit_now PASS\n", selected, idle_ms, k_uptime_get_32() - began);
}
int main(void) {
  printk("CLOCK_CHECK contract=0.13 native_ms=%u start\n", k_uptime_get_32());
  transaction(0, 75); transaction(0, 150); transaction(1, 75); transaction(2, 75);
  example_pair_init(&pair, &endpoints[0].core, &endpoints[1].core);
  const calculator_kernel_handlers_t handlers = {0};
  for (unsigned side = 0; side < 2; ++side)
    EXAMPLE_REQUIRE(calculator_kernel_init(&endpoints[side], &pair.ports[side], &handlers, NULL, NULL) == WL_OK);
  uint32_t now = read_clock(); unsigned before = reads; uint32_t began = k_cycle_get_32();
  for (unsigned i = 0; i < 20000; ++i) EXAMPLE_REQUIRE(calculator_kernel_step(&endpoints[0], now) == WL_OK);
  uint32_t cycles = k_cycle_get_32() - began;
  EXAMPLE_REQUIRE(reads == before);
  printk("CLOCK_CHECK idle iterations=20000 cycles/op=%u core_reads/op=0 endpoint_bytes=%u\n", cycles / 20000, (unsigned)sizeof(endpoints[0]));
  example_pair_close(&pair);
  printk("CLOCK_CHECK ALL PASS\n"); return 0;
}

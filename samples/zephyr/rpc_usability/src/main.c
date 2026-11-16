/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "rpc_validation.h"
#include "loopback_owner.h"
static rpc_validation_node_t endpoints[2];
static example_pair_t pair;
static unsigned completions, chained, executions;
static bool chain;
static response_t saved;
static large_t large_saved;
static wl_kernel_result_t terminal;
static request_t request(int32_t input) {
  return (request_t){.input = input,
    .name = {.length = 3, .data = {'a', 0, 'b'}}};
}
static int32_t execute(void *user, const request_t *input, response_t *output) {
  (void)user; ++executions;
  EXAMPLE_REQUIRE(input->name.length == 3 && memcmp(input->name.data, "a\0b", 3) == 0);
  if (input->input == -1 || input->input == INT32_MAX) return 17;
  output->output = input->input + 1;
  output->name.length = input->name.length; memcpy(output->name.data, input->name.data, input->name.length); return 0;
}
static int32_t download(void *user, const empty_t *input, large_t *output) {
  (void)user; (void)input; output->data.length = sizeof(output->data.data);
  memset(output->data.data, 0xa5, output->data.length); return 0;
}
static void done(void *user, wl_kernel_result_t result, const response_t *output) {
  (void)user; terminal = result;
  if (result.outcome == WL_KERNEL_SUCCESS) {
    EXAMPLE_REQUIRE(output != NULL);
    saved = *output;
    EXAMPLE_REQUIRE(saved.output == 42 && saved.name.length == 3 && memcmp(saved.name.data, "a\0b", 3) == 0);
  }
  ++completions;
  if (chain && !chained) {
    ++chained; request_t next = request(41); wl_kernel_call_handle_t handle;
    EXAMPLE_REQUIRE(rpc_validation_call_execute_ex(&endpoints[0], &next, pair.now, 500, done, NULL, &handle) == WL_OK);
  }
}
static void downloaded(void *user, wl_kernel_result_t result, const large_t *output) {
  (void)user;
  EXAMPLE_REQUIRE(result.outcome == WL_KERNEL_SUCCESS && output != NULL);
  large_saved = *output;
  ++completions;
}
static void setup(void) {
  example_pair_init(&pair, &endpoints[0].core, &endpoints[1].core);
  const rpc_validation_handlers_t handlers = {.on_execute = execute, .on_download = download};
  for (unsigned side = 0; side < 2; ++side)
    EXAMPLE_REQUIRE(rpc_validation_init(&endpoints[side], &pair.ports[side], &handlers, NULL) == WL_OK);
}
int main(void) {
  setup();
  wl_kernel_call_handle_t handle;
  for (unsigned slot = 0; slot < 4; ++slot) {
    request_t value = request(41);
    EXAMPLE_REQUIRE(rpc_validation_call_execute_ex(&endpoints[0], &value, pair.now, 500, done, NULL, &handle) == WL_OK);
    memset(&value, 0, sizeof(value));
  }
  request_t value = request(41);
  EXAMPLE_REQUIRE(rpc_validation_call_execute_ex(&endpoints[0], &value, pair.now, 500, done, NULL, &handle) == WL_ERR_WOULD_BLOCK);
  chain = true; example_pair_wait(&pair, &completions, 5); chain = false;
  EXAMPLE_REQUIRE(chained == 1 && executions == 5);
  value = request(-1);
  EXAMPLE_REQUIRE(rpc_validation_call_execute_ex(&endpoints[1], &value, pair.now, 500, done, NULL, &handle) == WL_OK);
  example_pair_wait(&pair, &completions, 6);
  EXAMPLE_REQUIRE(terminal.outcome == WL_KERNEL_REJECTED && terminal.status == 17);
  empty_t empty = {0};
  EXAMPLE_REQUIRE(rpc_validation_call_download_ex(&endpoints[0], &empty, pair.now, 500, downloaded, NULL, &handle) == WL_OK);
  example_pair_wait(&pair, &completions, 7);
  EXAMPLE_REQUIRE(large_saved.data.length == 2023);
  for (size_t i = 0; i < large_saved.data.length; ++i) EXAMPLE_REQUIRE(large_saved.data.data[i] == 0xa5);
  example_pair_close(&pair);
  EXAMPLE_REQUIRE(saved.output == 42 && large_saved.data.data[2022] == 0xa5);
  setup(); pair.now = UINT32_MAX - 2; value = request(41);
  EXAMPLE_REQUIRE(rpc_validation_call_execute_ex(&endpoints[0], &value, pair.now, 5, done, NULL, &handle) == WL_OK);
  EXAMPLE_REQUIRE(rpc_validation_step(&endpoints[0], pair.now) == WL_OK);
  /* The model driver retains the physical TX source; timeout is logical only. */
  pair.now += 5;
  EXAMPLE_REQUIRE(rpc_validation_step(&endpoints[0], pair.now) == WL_OK);
  EXAMPLE_REQUIRE(completions == 8 && terminal.outcome == WL_KERNEL_TIMED_OUT);
  EXAMPLE_REQUIRE(wl_packet_tx_active(&pair.ports[0]) && pair.cable.directions[0].pending);
  example_pair_close(&pair); /* Stop really abandons the model driver's borrow. */
  EXAMPLE_REQUIRE(completions == 8 && saved.output == 42);
  setup();
  EXAMPLE_REQUIRE(wl_kernel_cancel(&endpoints[0].core, handle) != WL_OK);
  example_pair_close(&pair);
  printk("RPC_OWNERSHIP ALL PASS slots=4 chained=1 bytes=2023 core_clock=none\n");
  return 0;
}

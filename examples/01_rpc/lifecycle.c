/* SPDX-License-Identifier: Apache-2.0 */
#include "service.h"
#include "loopback_owner.h"

static calculator_kernel_t endpoints[2];
static example_pair_t pair;
static unsigned completed, effects;
static struct completion {
  unsigned count;
  wl_kernel_result_t result;
} completions[5];

static int32_t add(void *user, const add_request_t *request, add_response_value_t *response) {
  ++effects;
  return calculator_add(user, request, response);
}

static void done(void *user, wl_kernel_result_t result, const add_response_t *response) {
  struct completion *completion = user;
  EXAMPLE_REQUIRE(completion->count == 0);
  completion->result = result;
  ++completion->count;
  ++completed;
  if (result.outcome == WL_KERNEL_SUCCESS)
    EXAMPLE_REQUIRE(response && response->sum == 42);
  else
    EXAMPLE_REQUIRE(response == NULL);
}

int main(void) {
  example_pair_init(&pair, &endpoints[0].core, &endpoints[1].core);
  const calculator_kernel_handlers_t handlers = {.on_rpc_add = add};
  for (unsigned side = 0; side < 2; ++side)
    EXAMPLE_REQUIRE(calculator_kernel_init(&endpoints[side], &pair.ports[side],
      &handlers, NULL, NULL) == WL_OK);
  wl_kernel_call_handle_t handles[4], rejected;
  add_request_value_t request = {.has_left = true, .left = 20, .has_right = true, .right = 22};
  for (unsigned i = 0; i < 4; ++i)
    EXAMPLE_REQUIRE(calculator_kernel_add_start(&endpoints[0], &request, 0, 2,
      done, &completions[i], &handles[i]) == WL_OK);
  EXAMPLE_REQUIRE(calculator_kernel_add_start(&endpoints[0], &request, 0, 2,
    done, &completions[4], &rejected) == WL_ERR_WOULD_BLOCK);
  EXAMPLE_REQUIRE(completed == 0);

  /* Hold the transport: one request is leased, the other three stay queued. */
  EXAMPLE_REQUIRE(calculator_kernel_step(&endpoints[0], 0) == WL_OK);
  EXAMPLE_REQUIRE(wl_kernel_cancel(&endpoints[0].core, handles[0]) == WL_OK);
  EXAMPLE_REQUIRE(completed == 0); /* cancel schedules, not invokes, completion. */
  EXAMPLE_REQUIRE(calculator_kernel_step(&endpoints[0], 1) == WL_OK);
  EXAMPLE_REQUIRE(completions[0].result.outcome == WL_KERNEL_CANCELLED);
  EXAMPLE_REQUIRE(wl_kernel_cancel(&endpoints[0].core, handles[0]) == WL_ERR_NOT_FOUND);
  EXAMPLE_REQUIRE(calculator_kernel_step(&endpoints[0], 2) == WL_OK);
  EXAMPLE_REQUIRE(completed == 4 && completions[4].count == 0);
  for (unsigned i = 1; i < 4; ++i)
    EXAMPLE_REQUIRE(completions[i].result.outcome == WL_KERNEL_TIMED_OUT);
  EXAMPLE_REQUIRE(wl_packet_tx_active(&pair.ports[0]) && effects == 0);

  /* Resume real copying. Local cancellation did not retract the leased request. */
  pair.now = 2;
  EXAMPLE_REQUIRE(calculator_kernel_add_start(&endpoints[0], &request, pair.now, 500,
    done, &completions[4], &rejected) == WL_OK);
  example_pair_wait(&pair, &completed, 5);
  EXAMPLE_REQUIRE(completions[4].result.outcome == WL_KERNEL_SUCCESS && effects == 2);
  example_pair_close(&pair);
  for (unsigned i = 0; i < 5; ++i) EXAMPLE_REQUIRE(completions[i].count == 1);
  puts("four calls, fifth blocked, cancel, three timeouts and recovery passed");
  return 0;
}

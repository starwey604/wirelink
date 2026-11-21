/* SPDX-License-Identifier: Apache-2.0 */
#include "calculator_fixture.h"
#include <limits.h>
#include "loopback_owner.h"

static calculator_node_t endpoints[2];
static example_pair_t pair;
static unsigned completed, effects;
static struct completion {
  unsigned count;
  wl_rpc_result_t result;
} completions[6];

static int32_t add(void *user, const add_request_t *request, add_response_t *response) {
  ++effects;
  (void)user;
  int64_t sum = (int64_t)request->left + request->right;
  if (sum < INT32_MIN || sum > INT32_MAX) return 1;
  response->sum = (int32_t)sum;
  return 0;
}

static void done(void *user, wl_rpc_result_t result, const add_response_t *response) {
  struct completion *completion = user;
  EXAMPLE_REQUIRE(completion->count == 0);
  completion->result = result;
  ++completion->count;
  ++completed;
  if (result.outcome == WL_RPC_SUCCESS)
    EXAMPLE_REQUIRE(response && response->sum == 42);
  else
    EXAMPLE_REQUIRE(response == NULL);
}

int main(void) {
  example_pair_init(&pair, &endpoints[0].core, &endpoints[1].core,
      &endpoints[0].port, &endpoints[1].port);
  const calculator_handlers_t handlers = {.on_add = add};
  for (unsigned side = 0; side < 2; ++side)
    EXAMPLE_REQUIRE(calculator_init_io(&endpoints[side], 0U, &pair.io[side],
      &handlers, NULL) == WL_OK);
  wl_call_handle_t handles[4], rejected;
  add_request_t request = {.left = 20, .right = 22};
  for (unsigned i = 0; i < 4; ++i)
    EXAMPLE_REQUIRE(calculator_call_add_ex(&endpoints[0], &request, 0, 2,
      done, &completions[i], &handles[i]) == WL_OK);
  EXAMPLE_REQUIRE(calculator_call_add_ex(&endpoints[0], &request, 0, 2,
    done, &completions[4], &rejected) == WL_ERR_WOULD_BLOCK);
  EXAMPLE_REQUIRE(completed == 0);

  /* Hold the transport: one request is leased, the other three stay queued. */
  EXAMPLE_REQUIRE(calculator_step(&endpoints[0], 0) == WL_OK);
  /* [docs:calculator_cancel_timeout] */
  if (wl_endpoint_cancel(&endpoints[0].core, handles[0]) != WL_OK) return 1;
  /* [/docs:calculator_cancel_timeout] */
  EXAMPLE_REQUIRE(completed == 0); /* Completion runs during step. */
  EXAMPLE_REQUIRE(calculator_step(&endpoints[0], 1) == WL_OK);
  EXAMPLE_REQUIRE(completions[0].result.outcome == WL_RPC_CANCELLED);
  EXAMPLE_REQUIRE(wl_endpoint_cancel(&endpoints[0].core, handles[0]) == WL_ERR_NOT_FOUND);
  EXAMPLE_REQUIRE(calculator_step(&endpoints[0], 2) == WL_OK);
  EXAMPLE_REQUIRE(completed == 4 && completions[4].count == 0);
  for (unsigned i = 1; i < 4; ++i)
    EXAMPLE_REQUIRE(completions[i].result.outcome == WL_RPC_TIMED_OUT);
  EXAMPLE_REQUIRE(wl_packet_tx_active(&endpoints[0].port) && effects == 0);

  /* Resume real copying. Local cancellation did not retract the leased request. */
  pair.now = 2;
  EXAMPLE_REQUIRE(calculator_call_add_ex(&endpoints[0], &request, pair.now, 500,
    done, &completions[4], &rejected) == WL_OK);
  example_pair_wait(&pair, &completed, 5);
  EXAMPLE_REQUIRE(completions[4].result.outcome == WL_RPC_SUCCESS && effects == 2);
  add_request_t overflow = {.left = INT32_MAX, .right = 1};
  EXAMPLE_REQUIRE(calculator_call_add_ex(&endpoints[1], &overflow, pair.now, 500,
      done, &completions[5], NULL) == WL_OK);
  example_pair_wait(&pair, &completed, 6);
  EXAMPLE_REQUIRE(completions[5].result.outcome == WL_RPC_REJECTED &&
      completions[5].result.status == 1);
  example_pair_close(&pair);
  for (unsigned i = 0; i < 6; ++i) EXAMPLE_REQUIRE(completions[i].count == 1);
  puts("four calls, fifth blocked, cancel, three timeouts and recovery passed");
  return 0;
}

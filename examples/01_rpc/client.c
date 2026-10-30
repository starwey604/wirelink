/* SPDX-License-Identifier: Apache-2.0 */
#include "service.h"
#include "loopback_owner.h"
unsigned calculator_async_count(void);
static calculator_kernel_t endpoints[2];
static example_pair_t pair;
EXAMPLE_COMPLETION(add, add_response)
int main(void) {
  example_pair_init(&pair, &endpoints[0].core, &endpoints[1].core);
  const calculator_kernel_handlers_t handlers = {.on_rpc_add = calculator_add};
  for (unsigned side = 0; side < 2; ++side)
    EXAMPLE_REQUIRE(calculator_kernel_init(&endpoints[side], &pair.ports[side], &handlers, NULL, NULL) == WL_OK);
  calculator_async_example(&endpoints[0], &endpoints[1]);
  for (unsigned pass = 0; pass < 500 && calculator_async_count() != 4; ++pass) example_pair_step(&pair);
  EXAMPLE_REQUIRE(calculator_async_count() == 4);
  /* A blocking application wrapper uses the same start/owner advancement. */
  add_request_value_t request = {.has_left = true, .left = INT32_MAX, .has_right = true, .right = 1};
  wl_kernel_call_handle_t handle;
  EXAMPLE_REQUIRE(calculator_kernel_add_start(&endpoints[1], &request, pair.now, 500,
    add_completed, NULL, &handle) == WL_OK);
  example_pair_wait(&pair, &add_count, 1);
  EXAMPLE_REQUIRE(add_result.outcome == WL_KERNEL_REJECTED && add_result.status == 1);
  example_pair_close(&pair);
  puts("four concurrent copied requests and reverse-direction rejection passed");
  return 0;
}

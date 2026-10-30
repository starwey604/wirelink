# RPC with synchronous handlers

A start snapshots the request. Four calls may coexist in each direction; a fifth returns WOULD_BLOCK. Completion happens exactly once for an accepted call, and a failed admission has no future callback. The handler returns the signed business status immediately; overflow rejects with status 1. The blocking application loop advances the same core rather than creating another RPC table.

This page uses current WLC/codegen 0.13 and profile v2. The C11 peers share one application owner and a real RAM-copy loopback, without opening hardware. Incrementing fixture instance IDs are only for this isolated example; production needs platform randomness or a durable unique allocator.

```sh
cmake -S . -B build/tutorials -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc-0.13
cmake --build build/tutorials --parallel
ctest --test-dir build/tutorials --output-on-failure
```

Run target `wirelink_calculator_demo` (under its examples subdirectory).

[01_rpc/client.c](../examples/01_rpc/client.c)

```c
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
```

[01_rpc/server.c](../examples/01_rpc/server.c)

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "service.h"
#include <limits.h>
int32_t calculator_add(void *user, const add_request_t *request, add_response_value_t *response) {
  (void)user;
  const int64_t sum = (int64_t)request->left + request->right;
  if (sum < INT32_MIN || sum > INT32_MAX) return 1;
  response->has_sum = true; response->sum = (int32_t)sum;
  return 0;
}
```

[Kernel API](static-endpoint-kernel-api.md) · [Migration](migration-0.13.md) · [Application jobs](tutorial-rpc-deferred.md)

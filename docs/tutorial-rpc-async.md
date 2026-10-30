# Four concurrent calls

The caller supplies now and a bounded timeout at admission. The outer owner passes its monotonic tick to step; the C kernel reads no clock. Local cancellation/timeout completes locally and cannot undo a request already executed remotely. Retrying creates a fresh operation and may repeat business execution. Results are borrowed during the completion callback only.

This page uses current WLC/codegen 0.13 and profile v2. The C11 peers share one application owner and a real RAM-copy loopback, without opening hardware. Incrementing fixture instance IDs are only for this isolated example; production needs platform randomness or a durable unique allocator.

```sh
cmake -S . -B build/tutorials -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc-0.13
cmake --build build/tutorials --parallel
ctest --test-dir build/tutorials --output-on-failure
```

Run target `wirelink_calculator_demo` (under its examples subdirectory).

[01_rpc/client_async.c](../examples/01_rpc/client_async.c)

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "service.h"
#include "loopback_owner.h"
static unsigned completed;
static void done(void *user, wl_kernel_result_t result, const add_response_t *response) {
  (void)user;
  EXAMPLE_REQUIRE(result.outcome == WL_KERNEL_SUCCESS && response && response->sum == 42);
  ++completed;
}
void calculator_async_example(calculator_kernel_t *client, calculator_kernel_t *server) {
  /* Four accepted calls coexist; no terminal slot needs inspect/release. */
  (void)server;
  for (int32_t i = 0; i < 4; ++i) {
    add_request_value_t request = {.has_left = true, .left = i, .has_right = true, .right = 42 - i};
    wl_kernel_call_handle_t handle;
    EXAMPLE_REQUIRE(calculator_kernel_add_start(client, &request, 0, 500, done, NULL, &handle) == WL_OK);
    memset(&request, 0, sizeof(request));
  }
}
unsigned calculator_async_count(void) { return completed; }
```

[Kernel API](static-endpoint-kernel-api.md) · [Migration](migration-0.13.md) · [Application jobs](tutorial-rpc-deferred.md)

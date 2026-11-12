# Call an RPC and handle rejection or timeout

[中文](tutorial-rpc-cn.md) · [Concurrent calls](tutorial-rpc-async.md)

Run the calculator: four requests return `42`, and signed addition overflow on the reverse call is rejected with business status `1`. The lifecycle executable then demonstrates local timeout and cancellation:

```text
four concurrent copied requests and reverse-direction rejection passed
four calls, fifth blocked, cancel, three timeouts and recovery passed
```

Run from the Wirelink root with matching WLC 0.13.0 (contract 0.13); see [tool setup](development.md#build-matching-wlc-and-generated-tutorials). The C11 endpoints share one owner and a RAM-copy loopback, without hardware. Its tick is one loop pass, not a millisecond. Fixture instance IDs are process-local; production uses a fresh platform identity.

```sh
cmake -S . -B /tmp/wirelink-docs-tutorials -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc
cmake --build /tmp/wirelink-docs-tutorials --parallel \
  --target wirelink_calculator_demo wirelink_rpc_lifecycle
ctest --test-dir /tmp/wirelink-docs-tutorials --output-on-failure \
  -R '^wirelink_(calculator_demo|rpc_lifecycle)$'
/tmp/wirelink-docs-tutorials/examples/01_rpc/wirelink_calculator_demo
```

```sh
/tmp/wirelink-docs-tutorials/examples/01_rpc/wirelink_rpc_lifecycle
```

## Start and receive a result

`calculator_kernel_add_start` takes an `add_request_value_t`, owner `now`, timeout, callback/context and output handle. Only `WL_OK` creates a call: input is snapshotted before return and any failure has no future completion. Here `500` means loop ticks. Application waiting advances the owner rather than blocking inside a handler.

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

## Return business status from a synchronous handler

Fill the response during this owner callback. Return `0` for success or a nonzero signed business status for rejection, which has no business body. Generated routing reserves reply storage before business decode/handler effects. A BUSY send of the retained reply does not run the handler again.

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

## Deadlines and remote effects

The [lifecycle source](../examples/01_rpc/lifecycle.c) admits four calls at tick `0` with timeout `2` while withholding transport progress. Step at `2` delivers `WL_KERNEL_TIMED_OUT / WL_ERR_TIMEOUT` for the three still-queued calls. Timeout includes queue time; equality expires before queued responses.

Cancel/timeout ends the local call, not an already leased request or remote execution. After transport resumes, the cancelled request still executes but cannot deliver a second terminal. A retry creates a new operation; use application idempotency keys where repeating effects is unsafe. See [terminal results](kernel-api.md#terminal-results) and continue with [retained results](tutorial-rpc-values.md).

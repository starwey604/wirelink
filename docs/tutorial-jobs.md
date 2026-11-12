# Accept and advance an application job

[中文](tutorial-jobs-cn.md) · [Concurrent calls](tutorial-rpc-async.md)

Run the device service: one StartSelfTest returns a nonzero job ID, the next Start is rejected with `DEVICE_BUSY`, and application-loop work later sends that job ID with its result in an ordinary message. Expect:

```text
device/calculator handlers, application job ID, ordinary completion and retained results passed
```

Run from the Wirelink root with matching WLC 0.13.0 (contract 0.13); see [tool setup](development.md#build-matching-wlc-and-generated-tutorials). The C11 endpoints share one owner and a RAM-copy loopback, without hardware. Its tick is one loop pass, not a millisecond. Fixture instance IDs are process-local; production uses a fresh platform identity.

```sh
cmake -S . -B /tmp/wirelink-docs-tutorials -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_WLC_EXAMPLES=ON \
  -DWIRELINK_WLC_EXECUTABLE=/absolute/path/to/matching-wlc
cmake --build /tmp/wirelink-docs-tutorials --parallel \
  --target wirelink_device_service_demo
ctest --test-dir /tmp/wirelink-docs-tutorials --output-on-failure \
  -R '^wirelink_device_service_demo$'
/tmp/wirelink-docs-tutorials/examples/03_device_service/wirelink_device_service_demo
```

## Return a job ID from a short RPC

[`device.wl`](../examples/03_device_service/device.wl) assigns StartSelfTest request `132`, response `133` and ordinary SelfTestProgress `134`. The handler admits one job into application-owned `device_service_t`; returning its ID completes that RPC. `self_test_poll` checks due time on the owner, calculates the result and tries an ordinary send. Snapshot pressure retains job state for the next attempt.

[03_device_service/self_test.c](../examples/03_device_service/self_test.c)

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "self_test.h"
#include "loopback_owner.h"
static int32_t immediate(void *user, const self_test_request_t *request,
                         self_test_response_value_t *response) {
  (void)user;
  response->has_observed = true;
  response->observed = request->pattern ^ UINT32_C(0xa5a5);
  return 0;
}
static int32_t start(void *user, const start_self_test_request_t *request,
                     start_self_test_response_value_t *response) {
  device_service_t *state = user;
  if (state->job_pending || state->next_job == UINT32_MAX) return DEVICE_BUSY;
  state->job_id = ++state->next_job;
  state->pattern = request->pattern;
  state->due = state->now + 20U;
  state->job_pending = true;
  response->has_job_id = true;
  response->job_id = state->job_id;
  return 0;
}
void self_test_bind(device_kernel_handlers_t *handlers) {
  handlers->on_rpc_self_test = immediate;
  handlers->on_rpc_start_self_test = start;
}
/* Application event-loop work, with ordinary TX backpressure. No reply token. */
void self_test_poll(device_service_t *state, device_kernel_t *endpoint) {
  if (!state->job_pending || (int32_t)(state->now - state->due) < 0) return;
  self_test_progress_value_t progress = {.has_job_id = true, .job_id = state->job_id,
    .has_observed = true, .observed = state->pattern ^ UINT32_C(0xa5a5)};
  const wl_err_t error = device_kernel_send_self_test_progress(endpoint, &progress);
  if (error == WL_ERR_WOULD_BLOCK) return;
  EXAMPLE_REQUIRE(error == WL_OK);
  state->job_pending = false;
}
```

## Inspect job state and completion

The `advance` function in [client.c](../examples/03_device_service/client.c) sets `service.now`, calls `self_test_poll`, then advances both endpoints. `due = now + 20U` is 20 application-loop ticks, not another RPC timeout. The owner can inspect `job_id`, `job_pending` and `due` directly. `job_pending` includes waiting for ordinary-message admission and clears only after admission; it does not prove peer receipt.

The client copies `job_id/observed` in its `progress` callback and compares the ID with the Start response. This example retains one job state, without a remote query RPC or completion history. For remote querying, define an application request keyed by `job_id`, running/done/unknown states, result retention and restart identity. Local RPC timeout cannot supply that information. Ordinary completion messages can be lost; production queries should return retained results, or the application should acknowledge completion.

## Separate work from reply lifetime

The handler borrows its request view only during invocation; job admission copies `pattern` into application state. Do not retain response scratch to reply later. Duplicate Start can execute again; local cancel/timeout cannot undo a job. The application defines idempotent admission, durable job identity and device actions during shutdown. An ordinary send returning `WL_OK` only admits a snapshot; the adapter still drives physical completion.

See [device services](../examples/03_device_service/README.md) for composition/extension tests, then [one owner loop](tutorial-integration.md) for transport integration.

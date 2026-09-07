# Beyond the basics: a server that cannot finish immediately

Immediate handlers suit short calculations and configuration reads. Waiting for
motion, Flash work, or a worker task inside a handler would also stall that
endpoint's RX, ACKs, and other calls. Choose deferred replies only when needed.
Clients can independently use sync or async.
[中文](tutorial-rpc-deferred-cn.md)。

## 1. Run a separate deferred server

Replace `calculator_server` with `calculator_server_deferred`; do not bind both to
the same ports. Both clients work unchanged, using the same schema and profile.

This small example retains an addition result and replies on the next owner-loop
pass. It neither simulates an actuator nor creates a worker thread. The lesson is
separating admission from completion. Complete
[server_deferred.c](../examples/01_rpc/server_deferred.c):

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include "calculator_advanced.h"
#include "tutorial_host.h"

typedef struct {
  calculator_endpoint_t *server;
  bool pending;
  calculator_add_request_token_t token;
  int64_t sum;
} job_t;

static int32_t enqueue(void *context, const add_request_t *request,
                       const calculator_add_request_token_t *token,
                       wl_delivery_t delivery) {
  job_t *job = context;
  (void)delivery;
  if (job->pending)
    return calculator_endpoint_add_reject(job->server, token, 2);
  printf("handling %ld + %ld\n", (long)request->left, (long)request->right);
  fflush(stdout);
  job->sum = (int64_t)request->left + request->right;
  job->token = *token;
  job->pending = true;
  return 0; /* Accepted locally, not a completed RPC response. */
}

/* The demo job becomes ready on the next owner-loop pass. A real worker
 * publishes a result and wakes this owner; it must not call complete itself. */
static wl_rpc_err_t finish(job_t *job) {
  add_response_t response;
  job->pending = false;
  if (job->sum < INT32_MIN || job->sum > INT32_MAX)
    return calculator_endpoint_add_reject(job->server, &job->token, 1);
  add_response_clear(&response);
  response.has_sum = true;
  response.sum = (int32_t)job->sum;
  return calculator_endpoint_add_complete(job->server, &job->token, &response);
}

int main(int argc, char **argv) {
  static calculator_endpoint_t server;
  calculator_endpoint_config_t config;
  job_t job = {0};
  uint16_t local = 49101, peer = 49100;
  CHECK(example_ports(argc, argv, &local, &peer));
  job.server = &server;
  CHECK(calculator_endpoint_config_defaults(&config, wl_platform_environment()) == WL_OK);

  config.advanced.add_request_handler = enqueue;
  config.advanced.add_user_data = &job;
  CHECK(calculator_endpoint_init_config(&server, &config) == WL_OK);
  example_udp_t *udp = example_udp_open(calculator_endpoint_handle(&server), local, peer);
  CHECK(udp != NULL);
  puts("calculator server ready (deferred)");
  fflush(stdout);
  while (example_running()) {
    if (job.pending) CHECK(finish(&job) == WL_RPC_OK);
    CHECK(calculator_endpoint_step(&server) == WL_OK);
    if (!job.pending) CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  CHECK(calculator_endpoint_close(&server) == WL_OK);
  job.pending = false; /* A saved token cannot outlive its endpoint. */
  example_udp_close(udp);
  return 0;
}
```

## 2. What the extra concepts do

- `calculator_advanced.h` explicitly selects advanced helpers. Do not register
  both immediate and deferred handlers for one service.
- A token is the framework-issued reply credential. Copy it whole; do not inspect
  its private fields or construct correlation IDs.
- `job_t` is this application's single work item, not a required endpoint
  assembly. It retains a numeric result and token, never the callback request pointer.
- An advanced handler returning 0 accepts work locally; it **does not reply**.
  Complete produces success; reject produces a nonzero business rejection.
  This example uses 1 for overflow and 2 for a busy application work slot.
  Returning nonzero alone abandons locally, without automatically sending rejection.
- Complete stores the response in framework storage; later steps transmit it.
  Successful completion submission does not mean the peer already received it.

Advanced requests/responses use codec views: string/bytes may borrow input.
Copy required fields, or explicitly use generated value-from-view conversion,
before deferring. Shallow-copying borrowed pointers is not a snapshot.
Ordinary immediate handlers still use owned values.

## 3. Connect a real slow task

Workers publish application results and wake the original owner. That owner calls
complete/reject; workers must not operate the endpoint directly. The single demo
job is application capacity, not a replacement for framework RPC capacity.

The demo finishes on the next pass. Real work needs suitable pending and application
deadlines. A client may already have timed out; tokens may expire or become invalid
after a peer-session change or close. Discard stale replies rather than inventing
new IDs, and finish the application task according to its own policy.
Stop/drain workers before close, discard tokens after close, and never reference
a destroyed endpoint. Timeout or local cancellation does not guarantee remote work stops.

See [advanced RPC runtime](rpc-runtime.md) for detailed error and lifetime contracts.


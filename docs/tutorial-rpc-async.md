# Beyond the basics: a nonblocking RPC client

If your program must also process buttons, rendering or other work, submit a
request and receive a completion later. Reuse the [calculator](tutorial-rpc.md)
schema and server; asynchronous calling does not change the wire protocol.
[中文](tutorial-rpc-async-cn.md)。

## 1. Run it

Start `calculator_server`, then run `calculator_client_async` in the same example
directory. Build instructions and port arguments match the synchronous client.
The normal output is still `20 + 22 = 42`.

## 2. Complete client

[client_async.c](../examples/01_rpc/client_async.c):

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "calculator_endpoint.h"
#include "tutorial_host.h"

typedef struct {
  bool done;
  wl_rpc_completion_t result;
  add_response_value_t response;
} addition_t;

static void completed(void *context, const wl_rpc_completion_t *result,
                       const add_response_value_t *response) {
  addition_t *addition = context;
  addition->result = *result;
  if (response != NULL) addition->response = *response;
  addition->done = true;
}

int main(int argc, char **argv) {
  static calculator_endpoint_t client;
  addition_t addition = {0};
  add_request_value_t request;
  uint16_t local = 49100, peer = 49101;
  CHECK(argc == 1 || argc == 3 || argc == 5);
  CHECK(example_ports(argc == 5 ? 3 : argc, argv, &local, &peer));
  add_request_value_clear(&request);
  request.has_left = request.has_right = true;
  request.left = 20;
  request.right = 22;
  if (argc == 5) {
    CHECK(example_int32(argv[3], &request.left));
    CHECK(example_int32(argv[4], &request.right));
  }

  CHECK(calculator_endpoint_init(&client, example_session_id(), example_clock()) == WL_OK);
  example_udp_t *udp = example_udp_open(calculator_endpoint_handle(&client), local, peer);
  CHECK(udp != NULL);
  CHECK(calculator_endpoint_add_async(&client, &request, 1500U,
      completed, &addition, NULL) == WL_OK);

  while (!addition.done && example_running()) {
    const int step = calculator_endpoint_step(&client);
    if (step != WL_OK) fprintf(stderr, "endpoint: %s\n", wl_err_str(step));
    if (!addition.done) CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  CHECK(calculator_endpoint_close(&client) == WL_OK); /* Also completes a call interrupted by Ctrl-C. */
  if (addition.result.status == WL_RPC_SUCCESS) {
    printf("%ld + %ld = %ld\n", (long)request.left, (long)request.right, (long)addition.response.sum);
  } else if (addition.result.status == WL_RPC_REJECTED) {
    printf("addition rejected: status=%ld\n", (long)addition.result.rejection);
  } else {
    fprintf(stderr, "RPC failed: %s\n", wl_rpc_status_str(addition.result.status));
  }
  example_udp_close(udp);
  return addition.result.status == WL_RPC_SUCCESS ||
         addition.result.status == WL_RPC_REJECTED ? 0 : 1;
}
```

`_async()` returning `WL_OK` means the request was snapshotted and accepted, not
that remote execution succeeded. Admission errors, such as a full queue, never
notify. Accepted calls notify once under continued driving or orderly close.
The framework recycles the call; application code sees a final outcome and
does not inspect or release slots.

`addition_t` is this application's chosen result state, not protocol storage or
an endpoint assembly. Callback result/response pointers expire on return. Copy
their values to retain them, including [bounded strings](tutorial-rpc-values.md).
A non-successful completion supplies a NULL response.

## 3. Who keeps communication moving?

The example steps its endpoint, then sleeps on UDP readiness instead of busy
polling. Integrate that work into your own UI/RTOS loop using
[platform integration](rpc-platform.md). Never drive one endpoint from two threads
or recursively step, sync, or close it from its own callback.

A callback may submit another async call or cancel other calls. To stop, set an
application flag and return to the owner loop before generated close. The Ctrl+C
path also notifies outstanding calls before closing UDP.
For cancellation, replace the final NULL submission argument with a
`wl_rpc_call_t` address, then call `calculator_endpoint_cancel()`.
Timeout and cancellation do not undo remote side effects.

For long-running server work, continue with [deferred replies](tutorial-rpc-deferred.md).


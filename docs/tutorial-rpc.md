# Lesson two: ask a device to calculate a sum

The [telemetry example](getting-started.md) sends measurements. Now a controller
asks a device to calculate 20 + 22 and return the result. This request/result
interaction is **RPC: remote procedure call**.

[Example 01_rpc](../examples/01_rpc/) contains only addition, not telemetry.
`calculator_client` initiates a call; `calculator_server` handles and replies.
Both use localhost UDP, but business code calls Wirelink rather than sockets.
[中文](tutorial-rpc-cn.md).

## 1. Run the programs

Use the previous build directory. After updating source, install matching
[WLC](installation.md) and rebuild:

```sh
cmake --build build/tutorials --config Release --parallel
```

Terminal A:

```sh
./build/tutorials/examples/01_rpc/calculator_server
```

After `calculator server ready`, terminal B:

```sh
./build/tutorials/examples/01_rpc/calculator_client
```

The client prints `20 + 22 = 42` and exits. The server stays running until Ctrl+C.
Visual Studio multi-configuration builds put executables under `01_rpc/Release/`
with an `.exe` suffix.

## 2. Define only business data

Complete [calculator.wl](../examples/01_rpc/calculator.wl):

```text
version 1;

message AddRequest @id(20) {
  required int32 left @id(1);
  required int32 right @id(2);
}

message AddResponse @id(21) {
  required int32 sum @id(1);
}
```

`AddRequest` contains the inputs; `AddResponse` contains the output.
`@id(20)` and `@id(21)` distinguish message types, not individual calls.
No Wirelink numbering or status fields are required in business messages.
The filename gives `calculator_endpoint_t` its business name; directory number
`01` controls reading order, not the C namespace.

## 3. Bind the input and output as an RPC

Complete [calculator.bind.wl](../examples/01_rpc/calculator.bind.wl):

```text
profile version 1;

rpc Add {
  request = AddRequest;
  response = AddResponse;
}
```

`rpc Add` names the service; `request` and `response` select message types.
**Both directions default to reliable delivery.** Wirelink supplies reliability;
UDP itself does not retransmit. Override a direction only when needed:

```text
rpc Add {
  request = AddRequest @delivery(unreliable);
  response = AddResponse;
}
```

Only the request becomes unreliable. This attribute belongs to the binding,
not the business schema. Defaults do not imply unlimited retries, no deadline,
or exactly-once business execution; running configuration still selects those limits.

Wirelink allocates and matches internal call numbers. Ordinary applications do not
save or compare them: completion notifies the caller and recycles the call.
An optional handle is needed only for explicit cancellation.

## 4. Client: provide arguments and wait for this result

Complete [client.c](../examples/01_rpc/client.c):

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "calculator_endpoint.h"
#include "tutorial_host.h"

int main(int argc, char **argv) {
  static calculator_endpoint_t client;
  add_response_value_t response;
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
  const wl_rpc_completion_t result = calculator_endpoint_add_sync(&client, &request, &response, 1500U);
  CHECK(calculator_endpoint_close(&client) == WL_OK);
  if (result.status == WL_RPC_SUCCESS) {
    printf("%ld + %ld = %ld\n", (long)request.left, (long)request.right, (long)response.sum);
  } else if (result.status == WL_RPC_REJECTED) {
    printf("addition rejected: status=%ld\n", (long)result.rejection);
  } else {
    fprintf(stderr, "RPC failed: %s (local=%s)\n", wl_rpc_status_str(result.status), wl_err_str(result.local_error));
  }
  example_udp_close(udp);
  return result.status == WL_RPC_SUCCESS || result.status == WL_RPC_REJECTED ? 0 : 1;
}
```

Focus on three parts:

1. Fill `add_request_value_t` and call `endpoint_add_sync()`. The final `1500U`
   is this call's millisecond budget, including local queuing—not the current time.
2. `result.status` reports success, rejection, timeout, cancellation or failure.
   Only success updates `response`. The response owns its data and survives endpoint close.
3. UDP attachment installs readiness waiting. The synchronous function drives communication,
   waits for socket activity or the next deadline, and reclaims the call. No application
   loop, callback, handle or `release` is needed here.

`result.rejection` is a business rejection; `result.local_error` describes local
admission/platform failures, such as a full queue or missing waiter. Keep them separate.
`example_udp_*` is [example platform support](../examples/common/tutorial_host.h), not another RPC.

This example creates no background thread. The caller is the sole endpoint owner and
may run local handlers while waiting. Never call sync from that owner's callbacks.
For business threads calling a background-owned endpoint, install the
[host executor proxy](rpc-platform.md) at setup; do not drive it from two threads.

An event loop that cannot block can use the separate
[client_async.c](../examples/01_rpc/client_async.c), built as `calculator_client_async`.
Its `_async()` snapshots accepted input and notifies on completion. Copy callback
`*response` to retain an independent result. Admission errors such as `WL_ERR_BUSY`
produce no callback. The server is unchanged. See [platform integration](rpc-platform.md)
for the detailed waiting and shutdown contract.

## 5. Server: fill a response or return a business rejection

Complete [server.c](../examples/01_rpc/server.c):

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include "calculator_endpoint.h"
#include "tutorial_host.h"

static int32_t add(void *context, const add_request_value_t *request,
                   add_response_value_t *response) {
  const int64_t sum = (int64_t)request->left + request->right;
  (void)context;
  printf("handling %ld + %ld\n", (long)request->left, (long)request->right);
  fflush(stdout);
  if (sum < INT32_MIN || sum > INT32_MAX)
    return 1; /* Business rejection; framework errors are separate. */
  response->has_sum = true;
  response->sum = (int32_t)sum;
  return 0;
}

int main(int argc, char **argv) {
  static calculator_endpoint_t server;
  calculator_endpoint_config_t config;
  uint16_t local = 49101, peer = 49100;
  CHECK(example_ports(argc, argv, &local, &peer));
  CHECK(calculator_endpoint_config_defaults(&config, example_session_id()) == WL_OK);
  config.clock = example_clock();
  config.on_add = add;
  CHECK(calculator_endpoint_init_config(&server, &config) == WL_OK);
  example_udp_t *udp = example_udp_open(calculator_endpoint_handle(&server), local, peer);
  CHECK(udp != NULL);
  puts("calculator server ready");
  fflush(stdout);
  while (example_running()) {
    CHECK(calculator_endpoint_step(&server) == WL_OK);
    CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  CHECK(calculator_endpoint_close(&server) == WL_OK);
  example_udp_close(udp);
  return 0;
}
```

`config.on_add = add` registers the service. The framework prepares the request,
clears the response, invokes the function and sends its outcome.
The handler needs no endpoint pointer, token or delivery parameter.
Use `config.add_user_data` only when the business function needs context.

Return 0 for success and fill every required response field. Nonzero values are
business rejection codes agreed by the peers; this example uses 1 for overflow.
Do not return `WL_ERR_*` to report a framework failure: those numbers would be
interpreted as business rejection codes. Codec/transport failures have a separate
diagnostic path. Long-running work uses the explicit
[advanced deferred-token API](rpc-runtime.md), not a blocking immediate handler.

## 6. Boundaries to remember

- Completion reports success, business rejection, timeout, cancellation or
  communication failure. Diagnostics in `wl_rpc_completion_t` belong to that
  call, not shared last_error state.
- Accepted calls receive exactly one notification under continued driving or
  orderly close. The response pointer is callback-scoped; copying `*response`
  preserves an independent value, including bounded string/bytes, without destruction.
- A callback may submit or cancel other calls. It must not recursively step or
  synchronously close its own endpoint. Set an application stop flag and close
  from the main loop; close notifies outstanding calls before returning.
- For cancellation, replace the final NULL with `&call` (`wl_rpc_call_t`) and
  call `calculator_endpoint_cancel(&client, &call)`. Completion still notifies
  the outcome. Timeout/cancellation does not undo remote side effects.
- Defaults provide four RPC slots but one link TX slot. Submission queues are
  bounded; Wirelink creates no thread, heap or unbounded queue.
  The recent-result cache may evict its oldest delivered response. Its 10-second
  TTL is a maximum age, not a promise of ten seconds of duplicate suppression.
  See [default endpoint](default-endpoint.md) for strict policy and capacity tuning.

Try changing the arguments, or run
`calculator_client 49100 49101 2147483647 1` to observe rejection.
Repeated clients need not wait for cache TTL. Wirelink handles acknowledgments
and retransmission over UDP, but this example supplies neither authentication
nor encryption and should not be exposed to untrusted networks.

Next: [integrate the endpoint into your program](tutorial-integration.md).

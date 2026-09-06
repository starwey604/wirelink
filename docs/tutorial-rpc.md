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

Wirelink allocates an internal call number, carries it in the request and reply,
and matches responses. Business code retains a **call handle**, like a receipt for
collecting that result. It never needs to compare or modify the number.
This is the default managed RPC mode.

## 4. Client: submit arguments and inspect the result

Complete [client.c](../examples/01_rpc/client.c):

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include "calculator_runtime.h"
#include "tutorial_host.h"

int main(int argc, char **argv) {
  static calculator_endpoint_t client;
  calculator_endpoint_config_t config;
  calculator_add_call_t call;
  calculator_add_result_t result;
  add_request_t request;
  uint16_t local = 49100, peer = 49101;
  CHECK(argc == 1 || argc == 3 || argc == 5);
  CHECK(example_ports(argc == 5 ? 3 : argc, argv, &local, &peer));
  add_request_clear(&request);
  request.has_left = request.has_right = true;
  request.left = 20;
  request.right = 22;
  if (argc == 5) {
    CHECK(example_int32(argv[3], &request.left));
    CHECK(example_int32(argv[4], &request.right));
  }

  CHECK(calculator_endpoint_config_defaults(&config, example_session_id()) == WL_OK);
  CHECK(calculator_runtime_config_enable_client(&config.runtime) == WL_OK);
  config.link.ack_timeout_ms = 100U;
  config.link.max_retries = 4U;
  CHECK(calculator_endpoint_init_config(&client, &config) == WL_OK);
  example_udp_t *udp = example_udp_open(calculator_endpoint_handle(&client), local, peer);
  CHECK(udp != NULL);
  /* The core's send clock comes from the latest owner step. */
  CHECK(calculator_endpoint_step(&client, example_now_ms()) == WL_OK);
  CHECK(calculator_endpoint_add_call(&client, &request, 1500U, example_now_ms(), &call) == WL_RPC_OK);

  for (;;) {
    const int step = calculator_endpoint_step(&client, example_now_ms());
    if (step != WL_OK) fprintf(stderr, "endpoint: %s\n", wl_err_str(step));
    CHECK(calculator_endpoint_add_inspect(&client, &call, &result) == WL_RPC_OK);
    if (result.state == WL_RPC_CLIENT_COMPLETED ||
        result.state == WL_RPC_CLIENT_APPLICATION_ERROR ||
        result.state == WL_RPC_CLIENT_TIMED_OUT ||
        result.state == WL_RPC_CLIENT_LINK_FAILED ||
        result.state == WL_RPC_CLIENT_CANCELLED) break;
    if (!example_running()) CHECK(calculator_endpoint_add_cancel(&client, &call) == WL_RPC_OK);
    CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  if (result.state == WL_RPC_CLIENT_COMPLETED && result.response_valid) {
    printf("%ld + %ld = %ld\n", (long)request.left, (long)request.right, (long)result.response.sum);
  } else if (result.state == WL_RPC_CLIENT_APPLICATION_ERROR) {
    printf("addition rejected: status=%ld\n", (long)result.application_status);
  } else {
    fprintf(stderr, "RPC failed: state=%ld\n", (long)result.state);
  }
  CHECK(calculator_endpoint_add_release(&client, &call) == WL_RPC_OK);
  example_udp_close(udp);
  return result.state == WL_RPC_CLIENT_COMPLETED ||
         result.state == WL_RPC_CLIENT_APPLICATION_ERROR ? 0 : 1;
}
```

Read it in business order:

1. Fill `left`, `right`, and their presence flags.
2. Enable the client role. This example waits 100 ms for link acknowledgements
   and permits four retransmissions.
3. `endpoint_add_call(..., 1500U, now, &call)` submits a call with a 1500 ms
   application-response deadline. `WL_RPC_OK` means submitted, not calculated.
   The core send clock comes from owner progress, so step once before the first call.
4. Keep stepping and inspecting. Successful inspection is not successful RPC:
   check `result.state`, then `response_valid` before using a successful body.
5. Release the call when finished, including failure, cancellation and timeout.
   Release or endpoint closure invalidates the old handle.

The `example_udp_*` and clock functions are the same
[platform support](../examples/common/tutorial_host.h) used previously, not another
RPC implementation. Socket readiness wakes the owner without millisecond idle polling.

## 5. Server: calculate and reply

Complete [server.c](../examples/01_rpc/server.c):

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include "calculator_runtime.h"
#include "tutorial_host.h"

static int32_t add(void *context, const add_request_t *request,
                   const calculator_add_request_token_t *token, wl_delivery_t delivery) {
  calculator_endpoint_t *server = context;
  const int64_t sum = (int64_t)request->left + request->right;
  (void)delivery;
  printf("handling %ld + %ld\n", (long)request->left, (long)request->right);
  fflush(stdout);
  if (sum < INT32_MIN || sum > INT32_MAX)
    return calculator_endpoint_add_reject(server, token, 1, example_now_ms());
  add_response_t response;
  add_response_clear(&response);
  response.has_sum = true;
  response.sum = (int32_t)sum;
  return calculator_endpoint_add_complete(server, token, &response, example_now_ms());
}

int main(int argc, char **argv) {
  static calculator_endpoint_t server;
  calculator_endpoint_config_t config;
  uint16_t local = 49101, peer = 49100;
  CHECK(example_ports(argc, argv, &local, &peer));
  CHECK(calculator_endpoint_config_defaults(&config, example_session_id()) == WL_OK);
  CHECK(calculator_runtime_config_enable_server(&config.runtime) == WL_OK);
  config.link.ack_timeout_ms = 100U;
  config.link.max_retries = 4U;
  config.runtime.rpc_server_pending_timeout_ms = 1000U;
  config.runtime.rpc_server_cache_ttl_ms = 10000U;
  config.runtime.add_request_handler = add;
  config.runtime.add_user_data = &server;
  CHECK(calculator_endpoint_init_config(&server, &config) == WL_OK);
  example_udp_t *udp = example_udp_open(calculator_endpoint_handle(&server), local, peer);
  CHECK(udp != NULL);
  puts("calculator server ready");
  fflush(stdout);
  while (example_running()) {
    CHECK(calculator_endpoint_step(&server, example_now_ms()) == WL_OK);
    CHECK(example_udp_wait(udp, 200U) == WL_OK);
  }
  example_udp_close(udp);
  return 0;
}
```

Registering `add_request_handler` makes endpoint progress invoke the handler.
`context` is simply the server pointer supplied by the application, not buffer assembly.

`calculator_add_request_token_t` identifies the request being answered.
`endpoint_add_complete()` prepares its successful reply; endpoint progress sends it.
There is no manual extraction/injection of call numbers.

Returning zero means locally accepted, not necessarily already transmitted.
Slow work may copy required parameters and the token, then reply later on the
same communication owner. A nonzero return abandons local handling;
**it does not automatically send a business rejection.**

## 6. Rejection, timeout and duplicate requests

The handler calculates in 64 bits to avoid signed overflow.
If the result cannot fit in 32 bits, `endpoint_add_reject(..., 1, now)` reports
the example's rejection status 1 without fabricating a sum. Try:

```sh
./build/tutorials/examples/01_rpc/calculator_client 49100 49101 2147483647 1
```

Expect `addition rejected: status=1`. The result has no valid response body;
`application_status` carries the reason.

Wirelink reliable delivery can retransmit lost UDP packets. Duplicate requests
within the retained cache scope do not execute addition again. A link ACK still
does not prove business completion. **Timeout/cancellation neither proves that
execution did not happen nor remotely undoes it.** A new call with identical
arguments is a new operation. Non-repeatable business actions need idempotency
or explicit state queries.

The default endpoint has one client slot; release it before starting another call.
Advanced storage supports concurrency. Until an ID is reused, late replies to
cancelled/timed-out/released calls remain diagnostic. Client reconstruction and
ID reuse have additional freshness limitations; see the [RPC contract](rpc-runtime.md).

## Next steps

Ports 49100/49101 form a fixed pair. This server is not a multi-client network
service. Reboot/reconstruction still requires the
[session and old-traffic rules](tutorial-integration.md#session-identity).

Read [integration](tutorial-integration.md) for installed packages and platform/storage
customization. Existing RPC field mappings and legacy `request_delivery = ...`
remain supported; declaring delivery twice for one direction is an error,
not a silent override.

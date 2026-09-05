# Lesson two: request a calculation

The [temperature example](getting-started.md) sent one-way updates. Now the
controller asks a device to calculate 20 + 22 and return the answer. It must
associate the answer with its request and stop waiting if no answer arrives.
This is **RPC**, a remote procedure call: request an operation at the other
end and wait for its application result.

We still use an in-memory connection. The complete example retains telemetry
and then performs one addition. [中文](tutorial-rpc-cn.md).

## 1. Received is not executed

Reliable delivery asks the receiver to return an **ACK**, an acknowledgement.
If confirmation does not arrive, the link may retransmit within its configured
retry limit. An ACK confirms link reception, not successful application work.

RPC adds a response message: “this calculation succeeded; the answer is 42.”
Our exchange has a request, its ACK, a response, and its ACK. Both messages
use reliable delivery, but RPC and reliable delivery are distinct concepts.

## 2. Add request and response messages

Complete [`quickstart.wl`](../examples/getting_started/quickstart.wl):

```text
version 1;

enum AddStatus = 1 {
  ADD_OK = 0;
  ADD_REJECTED = 1;
}

message Telemetry = 10 {
  required uint32 sample = 1;
  required int32 temperature_centi_c = 2;
}

message AddRequest = 20 {
  optional uint32 operation_id = 1;
  required int32 left = 2;
  required int32 right = 3;
}

message AddResponse = 21 {
  optional uint32 operation_id = 1;
  optional AddStatus status = 2;
  required int32 sum = 3;
}
```

`AddRequest` carries operands; `AddResponse` carries the answer. Message IDs
20 and 21 identify message types, not individual calls. `AddStatus` describes
application outcomes: 0 means success, 1 rejection. We demonstrate success.

A **call identifier**, here `operation_id`, associates a response with one
request: request 7 gets response 7. It matters when more than one call exists.

The schema marks metadata optional so the application can supply only business
fields. Generated start fills the request identifier; complete fills the
matching response identifier and success status. Incoming RPC traffic still
requires nonzero identifiers and a response status. General message decoding
and RPC validation enforce different constraints.

## 3. Combine messages into an RPC service

Complete [`quickstart.bind.wl`](../examples/getting_started/quickstart.bind.wl):

```text
profile version 1;

latest Telemetry {
  delivery = unreliable;
}

rpc Add {
  request = AddRequest;
  response = AddResponse;
  request_operation_id = operation_id;
  response_operation_id = operation_id;
  response_status = status;
  request_delivery = reliable;
  response_delivery = reliable;
}
```

`rpc Add` names the service. `request` and `response` select its message types;
the delivery properties select how each is transmitted.

The middle properties are **field mappings, not runtime assignments**:

| Property | Look up the right-hand name in | Purpose |
| --- | --- | --- |
| `request_operation_id = operation_id` | `AddRequest` | Request call identifier |
| `response_operation_id = operation_id` | `AddResponse` | Identifier of the call being answered |
| `response_status = status` | `AddResponse` | Application outcome |

Can the two operation IDs differ? **Names can; values for the same call cannot.**
An existing schema might name the request field `request_id` and the response
field `reply_to`. Its binding can use:

```text
request_operation_id = request_id;
response_operation_id = reply_to;
```

Those fields must exist and be non-repeated `uint32` fields. Their field numbers
may differ too: they belong to separate messages. Separate mappings allow
existing message definitions without imposing a universal field name.

For request `request_id = 7`, the response must contain `reply_to = 7`.
A response carrying 8 cannot complete call 7; whether it matches another call
or is rejected depends on the pending calls.

New schemas can simply name both fields `operation_id`. The current grammar
requires both explicit mappings. Applications using generated `client_start()`
and `server_complete()` do not manually copy the identifier.

## 4. Reliable traffic needs an identity across reboots

Suppose a device restarts while old packets or acknowledgements remain in the
connection. Reusing sequence numbers alone could let an old ACK confirm new work.

`session_id` identifies one boot or communication instance. Reliable data
and acknowledgements carry the relevant session identity so the protocol can
distinguish old-session traffic. Nonzero means simply that 0 is reserved as invalid.

It is not an address selecting which device receives a packet. Wirelink connects
two ends and provides no node-address routing. The isolated example uses fixed
0x1001 and 0x2002 values for repeatability, not a production reboot policy.

One approach generates a fresh nonzero random value each boot, often called a
**boot nonce**: a random identifier for this startup. Another increments a
persistent boot counter before using it. Avoid reusing an identity while old
traffic might survive; random generation must account for collision probability.
This identifier is not authentication or an encryption key.

## 5. Run, then follow the application work

Use the build directory configured in lesson one:

```sh
cmake --build build/quickstart --target wirelink_getting_started
./build/quickstart/examples/wirelink_getting_started
```

```text
unreliable telemetry: sample=7 temperature=23.50 C
reliable RPC: 20 + 22 = 42
```

Both ends are still generated `quickstart_endpoint_t` objects.
Use `endpoint_config_defaults()` / `endpoint_init_config()` to enable the client
role on the controller and server role on the device. `config.link` controls
the connection; `config.runtime` controls message handling. These are initialization
settings, not separately driven objects. Storage and progress remain assembled.

1. Fill operands and call `quickstart_endpoint_add_start(..., 100U, 10U)`.
   100 is the timeout in milliseconds; 10 is the current time. Save the returned ID.
2. Device progress invokes `handle_add()`. The calculation is short, so the
   callback computes it and calls `quickstart_endpoint_add_complete()` to prepare a response.
3. Keep calling both endpoints' `endpoint_step()` to advance acknowledgements and responses.
4. Check `endpoint_add_inspect()`; at `WL_RPC_CLIENT_COMPLETED`, use
   `quickstart_add_client_decode()` to read the response.
5. Call `endpoint_add_release()`. Failed/timed-out terminal calls also require release.

`calculator_t` is application context passing the device and simulated time to
the handler, not communication storage. Request pointers are borrowed during
the callback; slow tasks should copy their operands and request token, then
ask the communication owner to submit completion later.

`runtime_result_ok()` checks one step, and `runtime_result_rpc_detail()` accesses
RPC details such as the allocated identifier. Completion is checked separately
with inspect. Results retain their runtime names, but ordinary calls create no
standalone runtime object.

Times 10 through 29 are simulated timestamps, not recommended task periods.
Real applications use a monotonic clock and arrange progress from state/wakeup hints.

## 6. Complete program

[`examples/getting_started.c`](../examples/getting_started.c):

```c
/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include "quickstart_runtime.h"
#include "wirelink/loopback.h"

#define CHECK(expression) do { \
  if (!(expression)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expression); \
    return 1; \
  } \
} while (0)

typedef struct {
  quickstart_endpoint_t *device;
  wl_time_ms_t now_ms;
} calculator_t;

static int32_t handle_add(void *context, const add_request_t *request,
                          const wl_rpc_server_request_t *token,
                          wl_delivery_t delivery) {
  calculator_t *calculator = context;
  add_response_t response;
  quickstart_runtime_result_t result;
  const int64_t sum = (int64_t)request->left + request->right;
  (void)delivery;
  if (sum < INT32_MIN || sum > INT32_MAX) return -1;
  add_response_clear(&response);
  response.has_sum = true;
  response.sum = (int32_t)sum;
  /* Preparing a response is synchronous; endpoint_step sends it later. */
  result = quickstart_endpoint_add_complete(calculator->device, token,
                                            &response, calculator->now_ms);
  return quickstart_runtime_result_ok(&result) ? 0 : -1;
}

int main(void) {
  static quickstart_endpoint_t controller, device;
  quickstart_endpoint_config_t client_config, server_config;
  calculator_t calculator = {&device, 0U};
  wl_loopback_t cable;
  telemetry_t telemetry, received;
  add_request_t request;
  add_response_t response;
  quickstart_runtime_result_t result;
  const quickstart_runtime_rpc_detail_t *detail;
  wl_rpc_client_result_t operation;
  uint32_t operation_id;

  CHECK(quickstart_endpoint_config_defaults(&client_config, 0x1001U) == WL_OK);
  CHECK(quickstart_endpoint_config_defaults(&server_config, 0x2002U) == WL_OK);
  CHECK(quickstart_runtime_config_enable_client(&client_config.runtime) == WL_OK);
  CHECK(quickstart_runtime_config_enable_server(&server_config.runtime) == WL_OK);
  client_config.link.ack_timeout_ms = 20U;
  client_config.link.max_retries = 2U;
  server_config.link.ack_timeout_ms = 20U;
  server_config.link.max_retries = 2U;
  server_config.runtime.rpc_server_pending_timeout_ms = 1000U;
  server_config.runtime.rpc_server_cache_ttl_ms = 10000U;
  server_config.runtime.add_request_handler = handle_add;
  server_config.runtime.add_user_data = &calculator;
  CHECK(quickstart_endpoint_init_config(&controller, &client_config) == WL_OK);
  CHECK(quickstart_endpoint_init_config(&device, &server_config) == WL_OK);
  CHECK(wl_loopback_connect(&cable, quickstart_endpoint_handle(&controller),
                           quickstart_endpoint_handle(&device)) == WL_OK);

  telemetry_clear(&telemetry);
  telemetry.has_sample = true;
  telemetry.sample = 7U;
  telemetry.has_temperature_centi_c = true;
  telemetry.temperature_centi_c = 2350;
  CHECK(quickstart_endpoint_send_telemetry(&device, &telemetry).domain == QUICKSTART_SEND_OK);
  CHECK(quickstart_endpoint_step(&device, 1U) == WL_OK);
  CHECK(quickstart_endpoint_step(&controller, 1U) == WL_OK);
  CHECK(quickstart_endpoint_read_telemetry(&controller, &received) == WL_OK);
  CHECK(received.sample == 7U && received.temperature_centi_c == 2350);

  add_request_clear(&request);
  request.has_left = true;
  request.left = 20;
  request.has_right = true;
  request.right = 22;
  result = quickstart_endpoint_add_start(&controller, &request, 100U, 10U);
  detail = quickstart_runtime_result_rpc_detail(&result);
  CHECK(quickstart_runtime_result_ok(&result) && detail != NULL);
  operation_id = detail->operation_id;

  /* Simulated milliseconds. Real applications use their monotonic clock. */
  for (calculator.now_ms = 10U; calculator.now_ms < 30U; ++calculator.now_ms) {
    CHECK(quickstart_endpoint_step(&controller, calculator.now_ms) == WL_OK);
    CHECK(quickstart_endpoint_step(&device, calculator.now_ms) == WL_OK);
  }
  CHECK(quickstart_endpoint_add_inspect(&controller, operation_id, &operation) == WL_RPC_OK);
  CHECK(operation.state == WL_RPC_CLIENT_COMPLETED);
  result = quickstart_add_client_decode(&operation, &response);
  CHECK(quickstart_runtime_result_ok(&result) && response.sum == 42);
  CHECK(quickstart_endpoint_add_release(&controller, operation_id) == WL_RPC_OK);

  quickstart_endpoint_close(&controller);
  quickstart_endpoint_close(&device);
  puts("unreliable telemetry: sample=7 temperature=23.50 C");
  puts("reliable RPC: 20 + 22 = 42");
  return 0;
}
```

## 7. Beyond a successful calculation

`ack_timeout_ms = 20` limits acknowledgement waiting, and `max_retries = 2`
allows two retransmissions. The 100 ms call timeout bounds waiting for the
application result. Server settings keep pending work for 1000 ms and completed
responses for 10000 ms. These are example policies; choose them for your device.

The response cache handles a repeated request after its original response was
lost, by replaying a retained result. A retry using the same identifier must
also use the same request content. Capacity, expiry, reboot, and session changes
bound this protection. RPC does not guarantee durable exactly-once execution.
For commands with side effects, a timeout does not prove the device did nothing.

Generated reception observes the peer session on reliable RPC requests. A
transition clears old-session pending/cache state before invoking the new
handler. Products with additional authority or control state must handle that
transition too; see the [integration lesson](tutorial-integration.md).

Next: [use your own project and hardware](tutorial-integration.md).
Consult the [RPC reference](rpc-runtime.md) when you need individual state contracts.

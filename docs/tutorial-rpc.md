# Lesson two: ask a device to calculate a result

The [temperature lesson](getting-started.md) only sends measurements. Now a controller
asks the device to calculate 20 + 22 and return the answer. This request/result
interaction is **RPC (remote procedure call)**. We still use an in-memory connection;
no board is needed. [中文](tutorial-rpc-cn.md).

## 1. Run it first

Use the previous build directory. After updating sources, install the matching
WLC from the [installation guide](installation.md) first.

```sh
cmake --build build/quickstart --target wirelink_getting_started
./build/quickstart/examples/wirelink_getting_started
```

Expected output:

```text
unreliable telemetry: sample=7 temperature=23.50 C
reliable RPC: 20 + 22 = 42
```

The program sends one temperature update, then calculates the sum. Focus on the calculation below.

## 2. Messages contain business data only

Complete [`quickstart.wl`](../examples/getting_started/quickstart.wl):

```text
version 1;

message Telemetry @id(10) {
  required uint32 sample @id(1);
  required int32 temperature_centi_c @id(2);
}

message AddRequest @id(20) {
  required int32 left @id(1);
  required int32 right @id(2);
}

message AddResponse @id(21) {
  required int32 sum @id(1);
}
```

`AddRequest` contains the two inputs; `AddResponse` contains the sum.
`@id(20)` and `@id(21)` identify message types, not individual calls.
No business field is reserved for an internal call number or Wirelink status.

## 3. Associate the request and response

Complete [`quickstart.bind.wl`](../examples/getting_started/quickstart.bind.wl):

```text
profile version 1;

latest Telemetry {
  delivery = unreliable;
}

rpc Add {
  request = AddRequest;
  response = AddResponse;
  request_delivery = reliable;
  response_delivery = reliable;
}
```

`rpc Add` names the service. `request` and `response` choose its input and output
types; the last two lines choose their delivery policies, both reliable here.

Wirelink allocates an internal call number, transmits it with the request and
response, and matches the result automatically. We call this default **managed RPC**.
These details do not belong in the business struct. Application code saves a
returned call handle and uses it to inspect the result; do not inspect or modify
the handle's private members.

## 4. Complete program

The generated endpoints own their communication storage. Configuration enables
client/server roles and installs the calculation handler. `calculator_t` is only
business context carrying the device endpoint and simulated time, not buffer glue.
`CHECK` is an example assertion which exits the desktop process on failure.

This is the complete [`examples/getting_started.c`](../examples/getting_started.c):

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
                          const quickstart_add_request_token_t *token,
                          wl_delivery_t delivery) {
  calculator_t *calculator = context;
  add_response_t response;
  const int64_t sum = (int64_t)request->left + request->right;
  (void)delivery;
  if (sum < INT32_MIN || sum > INT32_MAX) {
    /* Business rejection: no fabricated sum or status field is needed. */
    return quickstart_endpoint_add_reject(calculator->device, token, 1,
                                          calculator->now_ms);
  }
  add_response_clear(&response);
  response.has_sum = true;
  response.sum = (int32_t)sum;
  /* Preparing a response is synchronous; endpoint_step sends it later. */
  return quickstart_endpoint_add_complete(calculator->device, token,
                                          &response, calculator->now_ms);
}

int main(void) {
  static quickstart_endpoint_t controller, device;
  quickstart_endpoint_config_t client_config, server_config;
  calculator_t calculator = {&device, 0U};
  wl_loopback_t cable;
  telemetry_t telemetry, received;
  add_request_t request;
  quickstart_add_result_t operation;
  quickstart_add_call_t call;

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
  CHECK(quickstart_endpoint_add_call(&controller, &request, 100U, 10U,
                                    &call) == WL_RPC_OK);

  /* Simulated milliseconds. Real applications use their monotonic clock. */
  for (calculator.now_ms = 10U; calculator.now_ms < 30U; ++calculator.now_ms) {
    CHECK(quickstart_endpoint_step(&controller, calculator.now_ms) == WL_OK);
    CHECK(quickstart_endpoint_step(&device, calculator.now_ms) == WL_OK);
  }
  CHECK(quickstart_endpoint_add_inspect(&controller, &call, &operation) == WL_RPC_OK);
  CHECK(operation.state == WL_RPC_CLIENT_COMPLETED);
  CHECK(operation.response_valid && operation.response.sum == 42);
  CHECK(quickstart_endpoint_add_release(&controller, &call) == WL_RPC_OK);

  quickstart_endpoint_close(&controller);
  quickstart_endpoint_close(&device);
  puts("unreliable telemetry: sample=7 temperature=23.50 C");
  puts("reliable RPC: 20 + 22 = 42");
  return 0;
}
```

## 5. Follow one call

1. Fill `left`, `right`, and their `has_...` flags. Call
   `quickstart_endpoint_add_call(..., 100U, 10U, &call)`: 100 is the timeout
   in milliseconds and 10 is the current time. Success means local submission;
   save `call` to inspect the eventual result.
2. Keep calling `endpoint_step()` on both ends. Reception invokes `handle_add()`
   on the device. It computes the sum and calls `endpoint_add_complete()` to
   prepare a response; subsequent progress sends it.
3. `endpoint_add_inspect(&controller, &call, &operation)` returns `WL_RPC_OK`
   when inspection succeeds, even while pending. Check `operation.state`.
   Once completed with `response_valid`, read `operation.response.sum` directly.
4. Call `endpoint_add_release()` when finished. Failed, cancelled, and timed-out
   terminal calls also need release. Released handles are invalid; closing and
   reinitializing the endpoint invalidates its previous handles too.

The loop advances simulated milliseconds. Real applications supply monotonic time
from a single communication thread or main loop. Session constants `0x1001` and
`0x2002` are only for this isolated simulation; [integration](tutorial-integration.md#session-identity)
explains choosing identities across hardware reboots.

The default static endpoint reserves one client call slot. Release it before the
next call. The RPC engine supports concurrent calls with custom storage; replies
may arrive out of order and are still matched internally, without application numbering.

## 6. Rejection, timeout, and cancellation

The handler calculates using 64-bit arithmetic to avoid signed 32-bit overflow.
If the result does not fit, it calls `endpoint_add_reject(..., 1, now)`.
Here 1 is the example's business status meaning an out-of-range sum.
The caller observes `WL_RPC_CLIENT_APPLICATION_ERROR`, `application_status == 1`,
and `response_valid == false`. There is no fabricated sum or required status field.

A handler returning zero reports successful local handling, including deferring
work. A nonzero return reports a local handler failure; it does not automatically
send a business rejection. For slow work, copy the needed arguments and
`quickstart_add_request_token_t`, then complete or reject using that token on the
communication owner. Its contents need no interpretation.

A reliable ACK confirms link reception, not that the calculation finished.
Missing the application deadline produces `WL_RPC_CLIENT_TIMED_OUT`.
`endpoint_add_cancel()` marks a call cancelled. **Neither cancellation nor timeout
proves the operation did not execute, and neither remotely undoes it.** Applications
decide whether to retry; non-repeatable operations need business idempotency or
an explicit state query.

Late replies to cancelled, timed-out or released calls are ignored with diagnostics
as long as their internal IDs have not been reused. Ordinary code never compares
the internal call number. Response isolation across client reconstruction has
additional limits; see the [RPC contract](rpc-runtime.md).

## Next steps

Try `INT32_MAX` and `1` as inputs and change the result assertion to check the
rejection above. Continue to [integration](tutorial-integration.md) for real
transports, memory configuration, and scheduling.

Explicit operation/status field mappings remain available for existing protocols,
not as the beginner API. Mapped and managed RPC have different payload formats:
upgrade both peers together. See the [RPC contract](rpc-runtime.md) for mappings,
the 12-byte metadata layout, and replay limits.

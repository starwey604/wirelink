# Build a Wirelink endpoint

This guide builds two in-memory endpoints, sends lossy telemetry in one
direction, and completes a reliable typed RPC in the other. It is Wirelink's
equivalent of libcsp's loopback client/server example, but the abstraction is
different: Wirelink is a point-to-point link engine, not an addressed network
or router. An application combines an endpoint, a transport, a session, and an
optional WLC-generated runtime.

## Mental model

```text
typed messages and RPC       application policy
WLC-generated runtime        encode, route, retain, correlate
Wirelink link                frame, integrity, ACK, retry, deduplicate
port/adapter                  UART, USB, UDP, CAN packet, or test loopback
```

One consumer owns sending, polling, event release, transaction completion,
runtime service, and adapter service. A transport callback or ISR may be the
single RX producer. Wirelink allocates no memory and creates no threads or
clocks.

| libcsp concept | Wirelink equivalent |
| --- | --- |
| addressed node | one point-to-point endpoint; addressing is out of scope |
| node incarnation | nonzero `session_id` on reliable traffic |
| destination port | stable WLC message ID |
| connection-less send | unreliable delivery |
| reliable connection/request | reliable transaction plus optional RPC |
| interface and driver | adapter using the public port API |
| router task | application-owned poll/service loop |

## Build the runnable example

From a source checkout with a compatible ABI 17 `wlc` executable:

```sh
cmake -S . -B build/quickstart \
  -DCMAKE_BUILD_TYPE=Release \
  -DWIRELINK_BUILD_GETTING_STARTED=ON \
  -DWIRELINK_WLC_EXECUTABLE=/path/to/wlc
cmake --build build/quickstart --target wirelink_getting_started
./build/quickstart/examples/wirelink_getting_started
```

Expected output is:

```text
unreliable telemetry: sample=7 temperature=23.50 C
reliable RPC: 20 + 22 = 42
```

The complete program is
[`examples/getting_started.c`](../examples/getting_started.c). Its in-memory
sink represents a packet transport; replacing that sink and ingress path is
the only transport-specific step.

## 1. Define the application protocol

[`quickstart.wl`](../examples/getting_started/quickstart.wl) assigns permanent
numeric IDs to `Telemetry`, `AddRequest`, and `AddResponse`. The separate
[`quickstart.bind.wl`](../examples/getting_started/quickstart.bind.wl) selects
runtime behavior:

```text
latest Telemetry { delivery = unreliable; }

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

Use unreliable delivery for replaceable state where a newer sample makes an
older one irrelevant. Use reliable delivery when link acknowledgement matters.
Use RPC when the caller needs an application result, deadline, status, or
bounded duplicate/replay handling.

## 2. Generate and link the typed runtime

An application consuming an installed Wirelink package needs only:

```cmake
find_package(Wirelink CONFIG REQUIRED)
wirelink_wlc_generate(
  TARGET quickstart_protocol
  SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/quickstart.wl"
  PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/quickstart.bind.wl")
target_link_libraries(my_endpoint PRIVATE quickstart_protocol)
```

Generation produces codecs, typed send functions, retained-message accessors,
RPC client/server functions, and a manifest containing schema, profile, and
codegen ABI identities. Generated C is compiled into the target; WLC does not
run on the device.

## 3. Initialize an endpoint and session

Choose the same envelope, integrity mode, payload bound, and transmission-unit
bound at both ends. These profile values are configured out of band in
protocol v1. Allocate the buffers returned by `wl_config_requirements()`, keep
them alive, and initialize the link with `wl_init()`.

A session ID is a nonzero boot/session incarnation, not a node address. It is
included in reliable DATA and ACK frames so a peer can distinguish retransmits
from traffic sent before a reboot. Provision it from a random boot nonce or a
persistent monotonic boot counter; do not reuse it while old frames may remain
in the transport.

The example uses the native-packet envelope and fixed static arrays for
clarity. A datagram transport feeds each complete unit through
`wl_feed_unit()`. A byte stream uses `wl_feed_bytes()` or the reserve/commit
producer API instead.

## 4. Bind the transport

Register one `wl_sink_fn` with `wl_set_sink()`:

- return `WL_SINK_SENT` when the bytes were consumed synchronously;
- return `WL_SINK_STARTED` when the transport borrows them asynchronously,
  then call `wl_tx_complete()` exactly once;
- return `WL_SINK_BUSY` for retryable backpressure; or
- return `WL_SINK_FAILED` for a terminal I/O failure.

The sink-provided byte pointer remains owned by Wirelink until synchronous
return or asynchronous completion. RX publication only makes work available;
decoding and application callbacks stay on the consumer.

## 5. Initialize application runtime storage

Set only the roles this endpoint owns. The controller example enables one RPC
client slot; the device enables one pending server operation, one replay-cache
entry, and the `Add` handler. Start from `quickstart_runtime_config_defaults()`,
then call `quickstart_runtime_config_enable_client()` or
`quickstart_runtime_config_enable_server()`. Defaults never invent RPC expiry
policy, so set nonzero pending/cache timeouts when the product requires them.

Because every quickstart RPC payload has a schema bound, WLC emits
`quickstart_runtime_default_storage_t`. Keep one beside the instance and pass
`quickstart_runtime_default_storage_descriptor()` to init; schema growth then
changes the C type instead of silently exceeding a guessed byte array. Advanced
configurations with larger slot counts can still use
`quickstart_runtime_requirements()` and a custom aligned arena.

The instance and storage arena must remain at stable addresses. Initialize a
`quickstart_runtime_pump_t` beside the runtime; it is caller-owned state, not a
thread or scheduler.

## 6. Drive events and RPC progress

Build the generated application hooks once, optionally fill the separate
adapter callback fields, and execute one bounded owner pass:

```c
quickstart_runtime_pump_t runtime_pump;
quickstart_runtime_pump_init(&runtime_pump, &runtime.instance.runtime,
                             observe_result, app);
wl_pump_hooks_t hooks = quickstart_runtime_pump_hooks(&runtime_pump);
hooks.adapter_user_data = transport;
hooks.service = transport_service;
hooks.quiesce = transport_quiesce;
hooks.adapter_deadline_hint = transport_deadline;

wl_pump_result_t step;
wl_pump_step(&endpoint.link, now_ms, 16U, &hooks, &step);
```

The bridge uses the pump's single time sample, releases RX events, reclaims
matching RPC terminals, services at most one queued response per pass, and
merges RPC deadlines. A successful response submission requests another
bounded pass; backpressure waits for transport progress instead of spinning.
Call `wl_pump_get_hint()` before sleeping. Advanced owner loops may still call
the generated dispatch/service functions directly and use `event_consumed`
before applying fallback ownership.

For ordinary result handling, use the generated helpers instead of selecting
the diagnostic union directly:

```c
if (!quickstart_runtime_result_ok(&result)) {
  log_error(quickstart_runtime_result_str(&result));
  return;
}
const quickstart_runtime_rpc_detail_t *rpc =
    quickstart_runtime_result_rpc_detail(&result);
if (rpc != NULL) {
  remember_operation(rpc->operation_id);
}
```

The detail accessor returns `NULL` when the result has a different detail tag.
Result strings are intended for logs; branch on `domain` or typed error fields,
not on their text.

Start an RPC with `quickstart_add_client_start()`, retain its returned nonzero
operation ID, inspect/decode the terminal response, and finally call
`quickstart_add_client_release()`. A retry may put that ID back into the same
canonical request to address the peer's bounded replay cache. Cache eviction,
expiry, session change, or restart ends this protection.

## 7. Move from loopback to hardware

Keep the schema, runtime, owner loop, and session rules unchanged. Replace the
memory sink with a platform adapter and select its matching ingress mode:

- UART/serial stream: COBS envelope plus byte or DMA publication;
- USB, UDP, or packet CAN: native-packet envelope plus unit publication;
- a bus that supplies a 16-bit length: `WL_ENVELOPE_BUS_LENGTH16`.

Start and stop the adapter outside the core, notify the owner on RX, TX
completion, and writability, and quiesce it before reinitializing endpoint
storage.

## Design reference

The presentation follows libcsp's separation of initialization, buffers,
send/receive flow, interfaces, and a default loopback client/server example:

- [The basics of CSP](https://libcsp.github.io/libcsp/basic.html)
- [Client and server example](https://libcsp.github.io/libcsp/example.html)
- [How to install LibCSP](https://libcsp.github.io/libcsp/INSTALL.html)

Wirelink deliberately stops below libcsp's node addresses, sockets, routing
table, router task, and standard network services.

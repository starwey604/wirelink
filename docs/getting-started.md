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

CRC detects accidental corruption; it does not authenticate a peer. Wirelink
v1 also has no encryption, discovery, broadcast, routing, or access control.
Put the link inside an authenticated/encrypted transport when required.

## Build the runnable example

From a source checkout with a compatible ABI 18 `wlc` executable:

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
wirelink_wlc_generate_codec(
  TARGET quickstart_codec
  SCHEMA "${CMAKE_CURRENT_SOURCE_DIR}/quickstart.wl")
wirelink_wlc_generate_runtime(
  TARGET quickstart_protocol
  CODEC_TARGET quickstart_codec
  PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/quickstart.bind.wl")
target_link_libraries(my_endpoint PRIVATE quickstart_protocol)
```

The codec target owns the data model, codecs, and typed send functions exactly
once. Each runtime target owns only its profile-specific retained-message and
RPC APIs. This permits host/device roles to share a schema in one process
without duplicate codec symbols or unused retained storage. Generated C is
compiled into the targets; WLC does not run on the device.

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

For hardware-free bring-up, bind both native-packet links to the supported
loopback adapter:

```c
wl_loopback_t transport;
wl_loopback_init(&transport, &controller.link, &device.link);

wl_loopback_service_result_t service;
wl_loopback_service(&transport, 4U, &service);
```

The adapter borrows each encoded unit asynchronously, models one-unit
backpressure in each direction, and completes it during a bounded `service()`
pass. It adds no payload buffer or heap allocation. Quiesce it before either
link's storage is reinitialized.

Hardware adapters implement the same port contract by registering one
`wl_sink_fn` with `wl_set_sink()`:

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

During bring-up, replace the final init call with the checked form to identify
the exact invalid field or undersized arena:

```c
application_runtime_t runtime;
quickstart_runtime_config_t config;
quickstart_runtime_storage_t storage;
quickstart_runtime_init_diagnostic_t diagnostic;

quickstart_runtime_config_defaults(&config);
quickstart_runtime_config_enable_client(&config);
storage = quickstart_runtime_default_storage_descriptor(&runtime.arena);
int rc = quickstart_runtime_init_checked(&runtime.instance, &config, &storage,
                                         &diagnostic);
if (rc != WL_OK) {
  log_init_error(quickstart_runtime_init_issue_str(diagnostic.issue),
                 diagnostic.field, diagnostic.required, diagnostic.provided);
}
```

The ordinary `quickstart_runtime_init()` has the same runtime outcome and is
the preferred small firmware path after configuration is proven. With the
usual function/data sections and linker garbage collection, leaving the
checked function unreferenced removes its validation and strings.

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

A reliable server request also establishes the generated runtime's current
peer session. The first binding and every transition set `rpc->peer_changed`.
Take the stored observation once to update product state outside RPC:

```c
if (rpc != NULL && rpc->peer_changed != 0U) {
  wl_rpc_peer_observation_t peer;
  if (quickstart_runtime_peer_observation_take(
          &runtime.instance.runtime, &peer) == WL_RPC_OK) {
    revoke_old_peer_leases(peer.previous_session_id, peer.current_session_id);
  }
}
```

The transition discards pending/cached work from the old session and requests
cancellation of detached reliable responses before the new request handler is
called. If reliable non-RPC traffic establishes the same product session,
observe it explicitly with `quickstart_runtime_peer_observe()` before applying
that message. Steady-session RPC dispatch uses an inline comparison and avoids
the observer path.

Start an RPC with `quickstart_add_client_start()`, retain its returned nonzero
operation ID, inspect/decode the terminal response, and finally call
`quickstart_add_client_release()`. A retry may put that ID back into the same
canonical request to address the peer's bounded replay cache. Cache eviction,
expiry, session change, or restart ends this protection.

## 7. Move from loopback to hardware

Keep the schema, runtime, owner loop, and session rules unchanged. Replace
`Wirelink::loopback` with a platform adapter and select its matching ingress
mode:

- UART/serial stream: COBS envelope plus byte or DMA publication;
- USB, UDP, or packet CAN: native-packet envelope plus unit publication;
- a bus that supplies a 16-bit length: `WL_ENVELOPE_BUS_LENGTH16`.

Start and stop the adapter outside the core, notify the owner on RX, TX
completion, and writability, and quiesce it before reinitializing endpoint
storage.

## 8. Add optional diagnostics

Link `Wirelink::diagnostics` only in images that need consistent bring-up text.
Snapshot state through its owning API, append it to a caller buffer, and hand
the result to the product logger:

```cmake
target_link_libraries(my_endpoint PRIVATE Wirelink::diagnostics)
```

```c
#include <wirelink/diagnostics.h>

char text[256];
wl_rx_counters_t counters;
wl_diag_writer_t writer;

wl_rx_get_counters(&endpoint.link, &counters);
wl_diag_writer_init(&writer, text, sizeof(text));
if (wl_diag_format_rx_counters(&writer, &counters) == WL_OK) {
  product_log(text);
}
```

The formatter owns no I/O or heap. See [`diagnostics.md`](diagnostics.md) for
the covered link, adapter, retained, Bulk, and RPC snapshots.

## Design reference

The presentation follows libcsp's separation of initialization, buffers,
send/receive flow, interfaces, and a default loopback client/server example:

- [The basics of CSP](https://libcsp.github.io/libcsp/basic.html)
- [Client and server example](https://libcsp.github.io/libcsp/example.html)
- [How to install LibCSP](https://libcsp.github.io/libcsp/INSTALL.html)

Wirelink deliberately stops below libcsp's node addresses, sockets, routing
table, router task, and standard network services.

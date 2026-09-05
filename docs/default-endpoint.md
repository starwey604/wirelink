# Default endpoints: design and boundaries

Status: internal development, codegen ABI 20. Existing mapped RPC and codec bytes
are unchanged; the new managed RPC mode has its own metadata prefix. No new
package is released. [中文](default-endpoint-cn.md). Start with the
[temperature tutorial](getting-started.md) for ordinary use.

## Application objects

WLC generates `*_endpoint_t` for profiles with finite message bounds. Declare a
zero-initialized static object, initialize it, connect an adapter, and use typed
send/read/RPC operations and `endpoint_step()`. Applications no longer assemble
link buffers, runtime arenas, or pump glue themselves.

| Layer | Responsibility | Entry |
| --- | --- | --- |
| Generic endpoint | Link state, owner hooks, bounded progress, close | `wirelink/endpoint.h`, primarily for generators/adapters |
| Generated endpoint | Profile-derived storage, runtime assembly, typed operations | `*_endpoint_t` / `*_endpoint_*()` |
| Adapter | Driver lifecycle, RX publication, TX completion, wakeups | endpoint handle → `wl_endpoint_link()` / `attach()` |
| Application | Values, operation results, clock and scheduling | send/read/RPC/step |

`private_state` and generic `private_*` fields expose static layout, not mutable
application contracts. Publicly allocatable types need not expose their pointer wiring.

## Sizing and defaults

The payload bound is the maximum encoded size among profile-selected retained
messages and RPC requests/responses, including the managed RPC 12-byte prefix,
at least one byte. Unrelated large messages
do not contribute. Init defaults to native packets and CRC32C. Buffers reserve
for every supported envelope and one stream RX packet, allowing envelope changes
without guessed arrays. Packet-only users pay for unused stream storage;
advanced assembly remains available for minimum-memory deployments.

The runtime uses its existing default layout: one FIFO slot, one client call,
one pending server request, and one cached response. RPC roles default off and
retry/expiry policy is not invented. Use `endpoint_config_defaults()`, change
`config.link`, `config.runtime`, `event_budget`, `on_result`, or `user_data`,
then `endpoint_init_config()`. Descriptors may be temporary; callback context
must outlive its use.

Unbounded selected messages or bounds above the 2048-byte frame limit yield
`*_HAS_DEFAULT_ENDPOINT=0`, not an undersized default object. Add finite message
constraints or use advanced custom storage. Codec/runtime targets remain separate;
multiple named endpoints may share one codec.

## Progress, errors, lifecycle

Managed RPC business messages need no operation/status fields. Use a generated
`*_call_t` with `endpoint_*_call/inspect/release/cancel`; inspection gives a typed
result. Server `complete/reject` takes a copyable generated reply token, not an
application-managed number. Handles and tokens are private-in-use and scoped to
their endpoint incarnation. Existing explicit field mappings retain their older
API/encoding for interoperability. See the [RPC contract](rpc-runtime.md).

No threads, clocks, or heap are created. A step runs on one owner with a default
budget of 16 events, including attached adapter service, event release, TX-terminal
reclamation, and RPC progress. Success is not RPC completion; inspect the call.
An idle step succeeds. Reliable TX failures and dispatch errors propagate upward.

`endpoint_result()` preserves the first runtime error of the pass; later successes
cannot overwrite it. The next pass starts fresh. Core/service details are available
through `wl_endpoint_last_step(endpoint_handle(...))`. Configure `on_result` for
individual results or peer changes; normal non-RPC TX success is not reported as
an error. Unacknowledged traffic still promises no remote delivery.

Zero-initialize before first use. Reinitializing a live endpoint fails without
silently clearing it. Close stops its attached adapter and invalidates the endpoint;
it is idempotent and allows subsequent init. Release advanced borrowed views and
finish application access to runtime storage before closing. Never move/copy active state.

Loopback connect installs transport service/close/hint for both endpoints, which
share one owner. Closing either stops the whole simulated cable; close both before
destroying it. Other hardware adapters retain their driver APIs and need integration
hooks. Drivers bound directly without attach still require caller-managed shutdown.

## Copies and advanced integration

Copying `endpoint_read_*()` is generated only for retainable messages without
borrowed pointers; acquire/release happens internally and no-data leaves output
unchanged. Advanced borrowed reads remain available through `endpoint_runtime()`.
Sending preserves direct encoding into TX storage and adds no packet copy.

Default progress reclaims reliable TX terminals: do not take those handles again.
Use manual link/runtime assembly for independently managed handles, custom memory,
larger queues, or custom dispatch instead of editing endpoint internals.

## Validation scope

Generator cases exercise LATEST coalescing, reliable FIFO, RPC completion, shared
codecs, all envelope/integrity sizing combinations, close/reinit, invalid configs,
first-error retention across subsequent valid RX, and reliable TX timeout against
the real core. C11/C++20 headers and existing Cortex-M runtime size gates remain
covered. Zephyr pump cases exercise generic lifecycle and adapter service errors.
Board validation remains deferred.

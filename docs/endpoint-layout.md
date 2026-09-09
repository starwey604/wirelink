# Static endpoint layouts by local role and transport

Internal development, codegen ABI 30; no release/tag or main merge.
[中文及尺寸记录](endpoint-layout-cn.md).

## Declare deployment policy once

Keep RPC definitions in a shared service profile. In the server's local profile:

```wl
profile version 1;
endpoint {
  envelope = native_packet;
  rpc_role = server;
}
send DeviceTelemetry { delivery = unreliable; }
```

The client selects `rpc_role = client` and `latest DeviceTelemetry`. Compose with
`PROFILES services.bind.wl server.bind.wl`; no per-handler switches or buffer
definitions. See the [12-service example](../examples/03_device_service/README.md).

`envelope` selects frame packaging, not an adapter implementation:

- `native_packet`: packet-preserving transports such as UDP; no stream FIFO.
- `bus_length16`: complete units with a 16-bit length prefix; no stream FIFO.
- `cobs_stream`: byte-stream framing, including a receive FIFO.
- `any`: reserve the largest supported layout; select the envelope at runtime.

`rpc_role` selects local client, server, or `both` capability. Omitted properties
default to `any` / `both`, including when the block is absent. No RPC declarations
means no RPC components. Composed profiles allow only one endpoint block, even
if duplicate declarations agree; file order never overrides policy.

## What changes

Fixed envelopes size their own worst-case frame buffers. Client-only instances
omit server pending/cache slots, cached responses and request scratch. Server-only
instances omit client slots, retained client responses, submission queues and
sync state. Client configs omit ordinary handler fields; server ordinary headers
omit async/sync/cancel helpers. Immediate and deferred server handlers still work.

CRC32C bounds permit smaller checksums. The existing build-wide
`<PREFIX>_ENDPOINT_RPC_CAPACITY` sets slot count, default four; define it consistently
across every consuming translation unit. Runtime configuration cannot restore
an omitted role or change a fixed envelope: initialization returns
`WL_ERR_NOT_SUPPORTED`, before obtaining a session identity for such mismatches.

Some expert runtime pointers/diagnostics and mapped-RPC encoding union remain
shared. This is not complete symbol or byte elimination. There is no heap,
overlapping TX storage, ownership change, or wire-format change. Regenerate all
codec/runtime artifacts together for ABI 30. Local profile identities change;
the shared schema identity does not.

## Acceptance

The 12-service Cortex-M7 compile-time layout shrinks client **7,000 → 5,664 bytes**
and server **5,608 → 4,016 bytes**, at four slots. These are ARM EABI `sizeof`
values, not board CPU measurements or total firmware RAM. Detailed host/ARM
comparisons and limits are in the [measurement record](endpoint-layout-cn.md).

`cargo test --test endpoint_layout -- --nocapture` in the independent WLC workspace
prints sizes and executes packet, fragmented COBS and length16 RPC/telemetry
tests at one/four slots. It checks lifecycle, invalid configuration, bounded
responses, and C11/C++20 consumption. The device-service CTest exercises real
UDP sync/async/deferred calls and adds a thirteenth RPC without transport changes.

# Choose a connector and budget its storage

[中文](link-profiles-cn.md) · [Wire format](packet-protocol.md) · [Adapter binding](adapters.md)

Choose native packets for USB bulk/UDP, COBS/CRC16 for a UART byte stream, or
length16/NONE when the transport supplies a trustworthy length-delimited stream.
A profile-v2 endpoint selects one connector; there is no on-wire negotiation.

| `endpoint.envelope` | Framing / integrity | Appropriate boundary |
| --- | --- | --- |
| `native_packet` | native/NONE | Complete packets |
| `cobs_stream` | COBS/CRC16-CCITT-FALSE, zero delimiter | UART/serial bytes |
| `bus_length16` | Two-byte big-endian length, NONE | Length-delimited bytes |

Peers must agree on connector, schema/message IDs and each exchanged route's
codec. Send/direct route sets can differ by application role; they must still
agree on the bytes for shared IDs. CRC detects corruption, not an authenticated
peer. Native/NONE and length16/NONE provide no connector checksum.

## Generate an explicit deployment

The telemetry example uses this complete profile with its `telemetry.wl`:

```wl
profile version 2;
endpoint { envelope=native_packet; }

direct Telemetry {
  delivery = unreliable;
}

send Telemetry { delivery=unreliable; }
```

Use one `wirelink_wlc_generate(TARGET ... SCHEMA ... PROFILE ...)` CMake target
with a [matching local WLC](development.md#build-matching-wlc-and-generated-tutorials).
It emits public codec, typed route/storage, deployment/manifest and, for streams,
`*_kernel_transport.h/.c` for the selected connector. Composed profiles must
resolve to one explicit endpoint deployment. RPC role is `both` by default and
is the supported endpoint role; it does not create separate client/server cores.

Ordinary send/direct routes use `delivery = unreliable` with `schema_v1`
(default) or `positional_v1`. RPC uses `managed_tagged` and distinct request/
response IDs. Profile v2 rejects reliable delivery, replay/guarantee declarations,
mapped RPC, positional RPC and retained LATEST/FIFO route declarations. Use
independent containers for local retained data and application policy for
retry/jobs. See [payload formats](packet-protocol.md) and the complete
[telemetry example](../examples/00_telemetry/README.md).

## Separate logical slots from byte capacity

The [kernel capacity reference](kernel-api.md#capacities) defines logical limits.
WLC computes each selected message's finite encoded maximum under its chosen
codec. Unbounded strings/bytes and general repeated fields, including nested
ones, cannot size a kernel route. It rejects canonical frames exceeding the
protocol limit; a smaller buffer must not silently narrow the schema's domain.

For a business body bound `B`, native frame bounds are `4+B` for ordinary
messages, `17+B` for requests, and `max(17+B, 21)` for replies. The request/
success prefix includes the four-byte DATA header and up to 13 metadata bytes;
a rejection can require 21 bytes including the header. WLC emits:

- `*_KERNEL_REQUEST_FRAME_MAX`: at least `17`, covering all selected requests.
- `*_KERNEL_REPLY_FRAME_MAX`: at least `21`, covering all replies/rejections.
- `*_KERNEL_MESSAGE_FRAME_MAX`: at least `4`, covering ordinary send routes.
- `*_KERNEL_PACKET_FRAME_MAX`: maximum canonical frame, including selected
  incoming/direct routes and these storage minima.
- `*_KERNEL_WIRE_FRAME_MAX`: the selected connector's wire bound for that packet
  maximum, including framing/integrity.

The endpoint stores four request arrays, four reply arrays and one ordinary
array using their respective bounds, plus core/handler/typed reply scratch and
alignment. Use `sizeof(generated_endpoint_type)` for the object, not nine times
one global frame maximum. RX and connector/hardware storage are separate.

## Budget RX and TX independently

For a stream, TX scratch must cover the wire bound of `maximum_frame_size`;
each RX unit must independently cover the wire bound of
`maximum_rx_frame_size`. Both fields count **canonical** bytes. When the RX
field is `0`, connector init derives a limit from its own RX unit capacity:

- COBS/CRC16 chooses the largest canonical length up to `2052` whose complete
  wire bound fits that unit.
- length16/NONE chooses `min(unit_size - 2, 2052)` when the unit permits it.

A derived length below four fails init. A larger TX bound does not enlarge RX,
and a smaller TX bound does not reduce RX. For exact asymmetric receive limits,
set `maximum_rx_frame_size` explicitly in manual connector configuration and
provide its required wire capacity.

Generated `*_kernel_transport_init` requires `rx_unit_size` and TX `wire` span
at least `*_KERNEL_WIRE_FRAME_MAX`, with total RX storage for two units; it leaves
the RX canonical limit to this capacity derivation. The
[wire reference](packet-protocol.md#connector-bytes-and-capacity) defines bound
formulas and the `2052 / 2064 / 2054` maximum-frame example. Use
`wl_packet_stream_cobs_crc16_bound` or `wl_packet_stream_length16_none_bound`
for manual assembly.

Native USB additionally requires strict read headroom beyond the legal frame;
Astrial host RX capacity must be an IN-MPS multiple. Native UDP requires one
extra byte beyond maximum datagram size. UART DMA driver buffers and FIFO
staging need their own budgets and flow-control policy. These requirements
belong to the [adapter configuration](adapters.md), not the call-slot count.

## Verify the selected path

[`packet_stream` tests](../tests/core/packet_stream.c) cover asymmetric TX/RX
bounds, all native payload lengths and partial-feed pressure. WLC
`tests/kernel.rs` exercises both stream connectors with the shared RPC kernel;
`tests/codec_plan.rs` and `tests/static_max.rs` verify selected-codec bounds.
Installed `tests/package/wlc_current` builds native and COBS endpoints. Run them
through the [development guide](development.md); keep connector, schema/profile
and compiler identity with any physical measurement.

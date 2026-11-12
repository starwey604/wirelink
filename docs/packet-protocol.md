# Packet and RPC wire format

[中文](packet-protocol-cn.md) · [Kernel API](kernel-api.md) · [Connector selection](link-profiles.md)

To decode a capture, identify the connector, recover its canonical DATA packet,
read the message ID, then select that route's payload codec. Neither the DATA
header nor the business body carries a codec discriminator. Peers agree on
connector, schema, IDs and codec out of band.

## Canonical DATA packet

The kernel accepts ordinary Compact-v1 DATA: a four-byte native/NONE header
followed by `0..2048` payload bytes.

| Offset | Encoding |
| --- | --- |
| `0` | `0xA4`: marker `0xA0`, version `1` in bits `3..2`, ordinary DATA kind `0` in bits `1..0` |
| `1` | Reserved, exactly `0x00` |
| `2..3` | Nonzero `uint16_t` message ID, big-endian |
| `4..end` | Ordinary business body, or RPC metadata followed by business body |

The transport packet boundary supplies length. Native/NONE adds no length,
CRC or COBS bytes; maximum canonical frame size is `2052` bytes. RPC metadata
counts toward the `2048`-byte payload limit.

[`native_none.c`](../src/native_none.c) validates the prefix. Short input,
bad marker/reserved byte and zero ID return `WL_ERR_BAD_FRAME`; incompatible
version returns `WL_ERR_PROTOCOL_VERSION`; unsupported low-bit kinds return
`WL_ERR_NOT_SUPPORTED`; an oversized ordinary frame returns
`WL_ERR_FRAME_TOO_LONG`. `seal` writes only the header, rejecting an oversized
payload with `WL_ERR_PAYLOAD_TOO_LONG`. Read errors leave output unchanged.

Message ID `0x1234` has prefix `a4 00 12 34`. The
[`kernel_wire` fixture](../tests/core/kernel_wire.c) checks it with payload
lengths through `2048`, plus malformed/unsupported packets. A rejected packet
does not prevent processing the next valid packet. Independent
[frozen vectors](conformance-v1.md) also cover standalone frame tools; the
kernel accepts ordinary DATA without ACK/retry.

## Managed RPC metadata

Request and response IDs are distinct profile assignments. RPC uses managed
v4 metadata at the start of the DATA payload:

| Field | Encoding |
| --- | --- |
| Client session | Nonzero `uint64_t`, 8 bytes big-endian; client's communication instance |
| Operation | Nonzero `uint32_t`, 1–5 tagged operation bytes |
| Business status | Only on rejection: nonzero signed `int32_t`, 4 bytes big-endian two's-complement bits |
| Business body | Request/success: managed tagged codec; rejection: absent |

The first operation byte uses bit `0` for rejection, bits `6..1` for operation
bits `5..0`, and bit `7` for continuation. Later bytes carry the next seven
operation bits, least-significant group first, with continuation in bit `7`.
The fifth byte can use only five payload bits. Encoding must be minimal:
redundant zero groups, overflow, truncation, zero operation/session and rejection
with zero status are invalid. Requests cannot be rejections. Rejection status
must occupy the complete remaining four bytes, with no body or trailing bytes.

Metadata occupies `9..13` bytes for request/success and `13..17` for rejection.
`WL_KERNEL_RPC_HEADER_MAX` is `17`. These metadata-only vectors use session
`0x123456789abcdef0`, whose bytes are `12 34 56 78 9a bc de f0`:

| Operation / status | Bytes after session | Total metadata bytes |
| --- | --- | ---: |
| `1 / 0` | `02` | 9 |
| `63 / 0` | `7e` | 9 |
| `64 / 0` | `80 01` | 10 |
| `1 / -1` | `03 ff ff ff ff` | 13 |
| `UINT32_MAX / 0` | `fe ff ff ff 1f` | 13 |

The full series, including `INT32_MIN`/`INT32_MAX` rejection statuses, is in
[`rpc_vectors.h`](../tests/core/rpc_vectors.h), exercised by `kernel_wire`.
[`rpc_wire.c`](../src/rpc_wire.c) implements the public primitives from
[`kernel_wire.h`](../include/wirelink/kernel_wire.h).

The server echoes session/operation. The client matches response ID plus
instance/operation against a waiting, submitted call. Identity is not
authentication or an application idempotency key. The kernel performs no
automatic retry or duplicate suppression; independently admitted duplicates
may execute again. See [RPC use](tutorial-rpc.md) for timeout/cancel and side
effects, and [API results](kernel-api.md#terminal-results) for local errors.

## Select the business-body codec explicitly

The schema defines fields/domains; the binding chooses their wire representation
independently of the connector:

| Use | Profile codec | Wire identity / shape |
| --- | --- | --- |
| Ordinary message | `schema_v1` (default) | `schema-v1`: tagged fields |
| Ordinary message | `positional_v1` | `positional-v1`: optional bitmap, field-ID order |
| RPC request / successful response | `managed_tagged` (default) | `managed-rpc-v4-tagged-varuint32`: metadata above, then tagged body |

Profile v2 requires managed tagged RPC; positional and mapped RPC are rejected.
Standalone public codecs can describe more shapes than bounded kernel routes.
Schema syntax alone does not establish endpoint support.

### Tagged fields and scalar domains

Both tagged bodies use unsigned LEB128 keys
`(field_number << 3) | wire_type`, with field numbers `1..65535`. Wire types
are `0` varint, `1` eight-byte fixed, `2` length-delimited and `5` four-byte
fixed. Encoders order fields by ID. Decoders accept reordered known fields,
skip permitted unknown fields, and reject duplicate singular/packed fields
or wrong known-field wire types. Re-encoding discards skipped unknown data.

`bool` accepts only `0/1`. Unsigned integers retain their full 8/16/32/64-bit
domains; signed integers use full-domain ZigZag and minimal unsigned LEB128.
Narrow overflow is an error, not truncation. Enums have an open signed
`int32_t` domain, including undeclared values. `fixed64` is eight-byte
big-endian. The important difference is **scalar `fixed32`**:

- Public Schema-v1: wire type `5`, four big-endian bytes. Field `1`, value `128`
  is `0d 00 00 00 80`.
- Managed tagged: wire type `0`, minimal full-domain unsigned varint. The same
  field/value is `08 80 01`.

`float32`/`float64` use big-endian IEEE binary32/binary64 bits. Codecs copy bits
without numeric normalization, preserving signed zero, infinities and NaN
payloads. Packed numeric arrays use length-delimited, exact-count, big-endian
fixed-width elements, including packed `fixed32` in both formats.

### Presence, strings and bounded shapes

Optional fields retain `has_*`: absence uses a local default/zero with false
presence; a present value equal to its default still has true presence.
Required fields must be present. Strings are length-delimited valid UTF-8;
bounds count encoded bytes, not characters or an extra terminator. Embedded
NUL is valid and counts in length. Bytes carry arbitrary data. Views borrow
input; use owned conversion before a kernel callback returns.

Tagged codecs support acyclic nested messages, with nesting depth at most `8`.
Public Schema-v1 supports general repeated fields with caller-provided
array/count/capacity. Their occurrence count has no schema bound, so no finite
endpoint storage maximum exists. Kernel-selected messages require a finite
maximum: no general repeated fields or unbounded string/bytes, including
through nesting. Bounded string/bytes, bounded nested messages and fixed-count
packed arrays are supported within packet capacity.

Ordinary Positional-v1 supports flat scalar and fixed-count packed fields,
without nested/repeated/string/bytes, tags or unknown-field skipping; it rejects
trailing bytes. Scalar `fixed32` is varint there too. Optional bits and exact
schema agreement matter; see [Positional-v1](positional-v1.md).
The [Schema-v1 reference](schema-v1.md) defines the public tagged format.
WLC `src/codec_plan.rs`, `src/kernel_profile.rs` and `src/kernel_codegen.rs`
define selection and finite route bounds.

## Connector bytes and capacity

For canonical length `N`, including the four-byte header:

- **native/NONE:** transmit the `N` bytes as one packet.
- **COBS/CRC16:** CRC16-CCITT-FALSE over all `N` bytes (polynomial `0x1021`,
  initial `0xffff`, no reflection, final XOR `0`), append two CRC bytes
  big-endian, COBS-encode, append delimiter `00`. Worst-case wire length is
  `N + 2 + floor((N + 2) / 254) + 2`.
- **length16/NONE:** prefix the packet with its two-byte big-endian length `N`.
  Wire length is `N + 2`; the length excludes its own prefix. An empty DATA
  packet with ID `0x1234` is `00 04 a4 00 12 34`.

At `N=2052`, the respective bounds are `2052`, `2064` and `2054`. Use the
selected connector's bound function/generated macros rather than frame-tool
constants that include other packet kinds. TX scratch and each stream RX unit
must independently cover their configured wire bound. Profiles, default RX
limit derivation and physical USB headroom are explained in
[capacity selection](link-profiles.md) and [adapter configuration](adapters.md).

On COBS pressure, feed retains the unconsumed suffix; delimiter recovery drops
malformed/oversized units while allowing later units. Length16 rejects lengths
below four or above its configured canonical RX limit and discards the declared
unit. It has no COBS-style resynchronization delimiter after arbitrary byte
loss. Both connectors validate canonical DATA before commit.
[`packet_stream` tests](../tests/core/packet_stream.c) compare encoded bytes with
independent frame tools for every payload length `0..2048`, and cover partial
input, pressure and malformed units.

Run wire/connector fixtures using the core build in [development](development.md):

```sh
ctest --test-dir /tmp/wirelink-dev-core --output-on-failure \
  -R '^(kernel_wire|packet_stream)$'
```

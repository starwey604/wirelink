# Current packet and RPC wire

[Kernel API](static-endpoint-kernel-api.md).

The endpoint accepts ordinary Compact-v1 DATA only. A canonical native/NONE
packet has this prefix, followed by 0..2048 payload bytes:

| Byte | Value |
| --- | --- |
| 0 | `0xA4` (marker, version 1, ordinary DATA kind) |
| 1 | `0x00` |
| 2–3 | Nonzero message ID, big-endian |

The native packet boundary determines payload length. There is no CRC/COBS on
USB native/NONE. Reliable DATA and ACK kinds return NOT_SUPPORTED; a following
ordinary packet remains receivable. Invalid markers/version/reserved byte,
zero ID or oversized packets fail validation. The standalone frame tools and
[frozen vectors](conformance-v1.md) also retain historical reliable/ACK formats;
that does not enable them in the endpoint.

COBS/CRC16 appends big-endian CRC16-CCITT-FALSE to the canonical packet, COBS
encodes that sequence and appends a zero delimiter. length16/NONE prefixes the
canonical packet with its two-byte big-endian byte count. Connector bounds and
RX budgets are explicit; select one with profile v2. They are not negotiated.

Managed RPC retains the existing v4 metadata inside the DATA payload:
nonzero 64-bit client session (big-endian), canonically encoded tagged nonzero
32-bit operation ID, and on rejection a nonzero signed 32-bit business status
(big-endian). The first operation byte uses six operation bits, a rejection bit
and continuation; later bytes use seven bits. Rejections contain no business
body and cannot be requests. Metadata occupies 9..17 bytes, depending on
operation/status. Its public primitives are in `wirelink/kernel_wire.h`.
Matching validates response message ID plus client instance/operation; stale
results cannot complete another call.

Successful/request bodies use the selected generated tagged business codec.
Public tagged SchemaV1 and ordinary PositionalV1 remain separate codec formats;
RPC positional/mapped execution is rejected. Preserve field/message IDs and
complete schema domains during migration. A schema change needs its own
compatibility decision; changing a bound is not a core optimization.

The native DATA bytes and managed metadata match the previous ordinary wire,
while execution guarantees have changed. There is no automatic ACK/retry,
server replay cache, authentication or peer negotiation. A duplicate request
may execute again. Define retry/idempotency in the application, and configure
compatible peers out of band.

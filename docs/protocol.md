# Packet and payload formats

Wirelink carries typed messages in ordinary Compact-v1 DATA packets. A native
packet has a four-byte header followed by its payload. Managed RPC adds client
instance, operation ID and business status metadata inside that payload.

- [Packet and RPC wire format](packet-protocol.md): header, metadata, validation
  and connector framing.
- [Schema-v1](schema-v1.md): tagged public payload encoding.
- [Positional-v1](positional-v1.md): schema-dependent ordinary message encoding.
- [Conformance vectors](conformance-v1.md): byte-level fixtures for frame tools.

USB Bulk uses native packets with NONE integrity. UART uses COBS with CRC16.
Peers agree on schema, message IDs and connector before exchanging messages.

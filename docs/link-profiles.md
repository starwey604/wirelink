# Choosing a connector

A profile-v2 endpoint selects one connector for its transport:

| Profile envelope | Framing and integrity | Transport |
| --- | --- | --- |
| `native_packet` | Native packet, NONE | USB Bulk, UDP or another packet-preserving transport |
| `cobs_stream` | COBS, CRC16-CCITT-FALSE, zero delimiter | UART or serial byte stream |
| `bus_length16` | Two-byte big-endian length, NONE | Length-delimited stream |

Both peers must agree on the connector, schema and message IDs. WLC generates
static routing, message-specific storage bounds and the selected stream
connector into one `wirelink_wlc_generate` target.

See the [telemetry example](../examples/00_telemetry/README.md) for a complete
profile and the [packet format](packet-protocol.md) for byte-level encoding.
[Adapter lifecycle](adapters.md) defines physical I/O ownership and completion.

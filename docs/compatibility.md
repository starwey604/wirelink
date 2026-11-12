# Compatibility at the 0.13 cut

0.13 breaks core API/ABI, generated interfaces, profiles, adapters and SDK binding API. Rebuild all consumers with matching local WLC 0.13/codegen 0.13; old runtime output is not a fallback. Pre-1.0 version labels are not a promise of ABI stability.

Ordinary native DATA and managed v4 RPC metadata are preserved, as are public tagged SchemaV1 and explicitly selected ordinary PositionalV1 domains and frozen vectors. Reliable DATA/ACK execution, replay/TTL and deferred semantics are removed even though standalone frame tools retain their historical bytes. Coordinate schema/IDs/connector and application retry/idempotency out of band. Public payload bounds cannot be silently reduced for performance. See [packet format](packet-protocol.md) for accepted frames.

[Kernel API](static-endpoint-kernel-api.md) · [Development checks](development-current.md)

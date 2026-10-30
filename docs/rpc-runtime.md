# Bounded synchronous RPC

RPC uses one shared four-call table and typed synchronous handlers. Admission snapshots requests; handlers return a signed business status and fill an owned response during the owner callback. A reply reservation precedes effects. Encoded responses survive BUSY without rerunning the handler; no reply history survives for deduplication.

There is no replay/TTL, deferred token or terminal inspect/release. Save callback results with public value conversion. Timeout/cancel is local and retry may execute again. Long tasks acknowledge an application job ID and expose application status. The generated C, C++ Session and SDK use the same core rather than mirroring correlation tables.

[Migration checklist](migration-0.13.md) · [Kernel API](static-endpoint-kernel-api.md) · [Development checks](development-current.md)

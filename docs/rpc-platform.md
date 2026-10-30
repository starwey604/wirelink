# Host owner and waiting

`wirelink/host/executor.hpp` binds one C kernel and optional I/O callbacks. Ownership stays with the caller until start succeeds. The queue has 32 command slots; at most eight commands precede kernel work. Work or shutdown rejection executes on the owner. There is no RPC table, application scheduler or payload pool in Executor.

Owner callbacks may request_stop but cannot join/destroy the owner. External stop joins, closes logical work, stops actual producers and processes final completion before storage release. I/O notify must be nonblocking, latched, thread-safe and must not reenter callbacks. Set WIRELINK_BUILD_HOST_RUNTIME=ON; current queue/stop tests live in runtime/host/tests and benchmarks/owner_pass.

[Migration checklist](migration-0.13.md) · [Kernel API](static-endpoint-kernel-api.md) · [Development checks](development-current.md)

# Business handlers and application jobs

[中文](README-cn.md)

Build `wirelink_device_service_demo` with `WIRELINK_BUILD_WLC_EXAMPLES=ON` and
matching WLC 0.0.99 (codegen contract 1). Its two C11 endpoints share one deterministic
owner and RAM-copy loopback. Run `ctest -R wirelink_device_service` for the demo
and extension test. The example does not access hardware; UDP interoperability
is covered by the UDP/SDK examples.

`device.wl` holds messages, shared RPC declarations, and the
`device_client` / `device_server` nodes. Business state lives in
`device_service_t`. Service modules directly implement `device_server_on_*`
symbols, fill owned responses, and return signed business status. They obtain
application state from `self->user`; no registration table is needed. The
100-channel ordinary telemetry message participates in TX byte sizing.

StartSelfTest (message 132) returns job_id (133) immediately. Application polling
waits 20 owner ticks and sends ordinary SelfTestProgress (134). One job is active;
a second Start returns DEVICE_BUSY. Pending ordinary TX backpressure retains the
result until admission. The existing SelfTest RPC returns its calculation directly.
See [docs/README.md](../../docs/README.md) for the Wirelink documentation index.

The example tracks local job state and matches received progress by job ID; it
has no remote status-query RPC or completion history. Production protocols need
to define query/retention/restart behavior to recover lost ordinary progress.
Local cancel/timeout cannot undo a job, and repeated requests may execute again.

Completion callbacks copy borrowed views before returning. Saved values, including
a UTF-8 name containing NUL, remain readable after close and physical quiescence.
`device_service_extension.py` adds GetBuildLabel in three application files in a
temporary copy, builds/runs it through the same CMake/owner, and checks the original
sources remain unchanged. It does not modify transport or a generic RPC table.

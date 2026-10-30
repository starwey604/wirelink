# Application jobs replace deferred replies

Deferred reply tokens were removed. A handler fills its response and returns a
business status during the current owner callback. Do not retain a borrowed
response view, token or pointer into the core's stack to reply later.

The [device-service example](../examples/03_device_service/README.md) immediately
acknowledges StartSelfTest (message 132) with a job_id (133). Application state
advances the job in its own event loop and sends ordinary
SelfTestProgress(job_id, observed) (134) when ready. Those IDs extend this example
application protocol; the original SelfTest remains an immediate short
calculation, and benchmark schemas are unchanged.

Define duplicate Start handling, durable job identity, status queries and
recovery from lost completion messages in the application. The example has one
job, returns DEVICE_BUSY when occupied and retains an ordinary output until
admission succeeds; the core supplies no generic job service. Cancellation or
timeout cannot roll back a remote job; another RPC may execute it again.

Build with `WIRELINK_BUILD_WLC_EXAMPLES=ON` and local WLC 0.13. Run
`wirelink_device_service_demo`; `ctest -R wirelink_device_service` also verifies
adding an RPC in four application files with unchanged infrastructure.

[Migration](migration-0.13.md) · [Concurrent calls](tutorial-rpc-async.md)

# Product work belongs in the application

Generic endpoint policy/service descriptors and peer-observation hooks were
removed. Keep application deadlines, upload fairness, control leases, latest
values and jobs in an explicit owner loop. There is no framework job scheduler,
replay cache or inferred remote-reboot reset for application state.

The generated core only performs bounded typed dispatch, four-call correlation
and packet ownership. A caller snapshots admission inputs; handlers fill a
response immediately. Application work must remain bounded and use readiness
or a future deadline when blocked. Merge that hint with kernel and adapter
readiness, then wait on a latched wake. Ordinary TX backpressure requires the
application to retain its pending output until admission succeeds.

See [04_composed_services](../examples/04_composed_services/README.md) for two
independent ports/cores with application-owned fairness and business object
transfer, and [03_device_service](../examples/03_device_service/README.md) for
application job IDs. Their duplicate/commit behavior belongs to those protocols;
local cancel/close cannot undo a remote effect. The core does not promise
automatic retry, exactly-once execution or bandwidth reservation.

[Application layer](application-layer.md).

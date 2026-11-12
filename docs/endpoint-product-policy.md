# Schedule application work beside the kernel

[中文](endpoint-product-policy-cn.md)

Keep latest-value selection, ordered business work, jobs, retries and idempotency
in explicit application state. Independent `Wirelink::containers` FIFO/LATEST/outbox
primitives provide local storage rules; application code decides when to use them.
The kernel handles typed dispatch, four-call correlation and packet ownership.

Advance bounded business work in the owner loop. A blocked action provides
readiness or a future deadline; merge that hint with kernel/adapter readiness
and wait on a latched wake. Retain an ordinary output after `WL_ERR_WOULD_BLOCK`
until it is admitted. Do not report immediate work repeatedly without a possible
state transition.

[Composed services](../examples/04_composed_services/README.md) demonstrate two
ports/cores with application-owned fairness and object transfer.
[Application jobs](tutorial-jobs.md) demonstrate short admission, a job ID and
ordinary completion. Applications define durable identity, status queries,
result retention and shutdown actions. Local cancel/close cannot undo a remote
effect; retry may execute the business operation again.

See the [owner loop](tutorial-integration.md) for deadline/wait composition.

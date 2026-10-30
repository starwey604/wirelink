# Application state and scheduling

Application state owns latest-value selection, ordered business work, jobs, retries and idempotency. The core provides typed synchronous dispatch and four-call correlation, not policy/service hooks. Independent FIFO/LATEST/outbox primitives remain available via `Wirelink::containers`; choosing to use them does not create a reliable route or framework scheduler.

Advance application deadlines in the owner loop and merge their wait hints with kernel/adapter readiness. Retain an ordinary output on WOULD_BLOCK until it can be admitted; never advertise immediate work repeatedly without a possible transition. See examples/03_device_service and examples/04_composed_services for bounded application state and long-job status.

[Migration checklist](migration-0.13.md) · [Kernel API](static-endpoint-kernel-api.md) · [Development checks](development-current.md)

# Communication identity and C++ Session

A communication instance is fresh, nonzero and acquired at initialization through an explicit source. Platform randomness or a durable allocator is required; failure never falls back to a clock/fixed production ID. Comparing one previous identity is only a local guard, not a uniqueness proof. Operation/generation exhaustion requires quiescent restart. Identity is not authentication or a business idempotency key.

C++ `wirelink::Session` is an optional owning UDP wrapper around a C endpoint factory, the same Executor and kernel. Submit waits for bounded owner admission; success means request snapshot, failure creates no call. An Operation keeps itself alive to its terminal callback and owns copied typed results. External close excludes/waits for submitters and joins I/O/owner; results survive close. Owner close requests stop only and requires an external join. Destroying/replacing Session on its owner callback is prohibited. Manual-clock progress requires notify. Tests cover local cancellation, rejected admission, four concurrent calls, rejection status and close races.

[Kernel API](static-endpoint-kernel-api.md) · [Development checks](development-current.md)

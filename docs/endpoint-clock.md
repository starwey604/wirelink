# Explicit owner time

The C kernel receives `now` from its owner and reads no clock. Call timeout is 1..INT32_MAX ticks, using modular uint32 arithmetic. Sample one monotonic snapshot for related work, merge earliest kernel/application deadlines with adapter hints, then rebase the wait for elapsed owner work. Use a latched notification to close the query/wait race.

`wl_platform_environment` is an optional outer provider. It supplies clock and fresh-instance descriptors without a mutable global provider. Identity acquisition happens at initialization, never on the hot path. Custom clocks used by host submitters/owner must be thread-safe; manual-clock advancement explicitly notifies the Session/waiter. tests/clock_bridge exercises C-owned endpoints from C++/Python with one outer clock read and zero core reads.

[Kernel API](static-endpoint-kernel-api.md) · [Development checks](development-current.md)

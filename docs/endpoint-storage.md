# Caller-owned endpoint storage

Generated endpoints size four request snapshots, four reply snapshots, an ordinary snapshot and codec scratch from their declared messages. The packet port, exactly two RX units, connector TX/staging storage, driver buffers and outer wrapper state are separate budgets; do not double-count a generated endpoint's internal snapshots or omit driver memory.

The core never allocates. Optional fixed-block storage allocates an entire C-owned owner block; initialize it with a fresh identity and release only after real I/O stop and kernel quiescence. C++/Python bridges exercise allocation rollback and poison/free after close. Heap, fixed-pool or static placement is an outer choice; no allocation service enters the kernel.

[Migration checklist](migration-0.13.md) · [Kernel API](static-endpoint-kernel-api.md) · [Development checks](development-current.md)

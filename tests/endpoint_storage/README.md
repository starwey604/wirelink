# Explicit fixed-pool owner lifetime bridge

`WIRELINK_BUILD_FOREIGN_TESTS=ON` builds this C-owned shared library and opaque
C++ RAII/Python ctypes consumers. The test shim allocates its outer wrapper once;
two fixed-pool blocks each contain the complete generated kernel, packet port,
C atomic storage and two RX units. No generated create/destroy allocator facade
or separate runtime service is involved.

The application explicitly allocates both blocks, obtains deterministic fresh
fixture identities, initializes the port/kernel pair, and drives synchronous
calls through the real C callback path. Borrowed responses are copied before
callback return. Failure on the second allocation rolls back the first block.
Each foreign consumer runs 2000 calls with exactly two initialization allocations
and zero hot allocations. Close joins actual loopback cancellation/quiescence,
then poisons and frees both blocks. Repeated close is safe and a rejected call
after close leaves the caller's last owned result intact.

This is a single-owner RAM integration test, not a production blocking wrapper
or a concurrent allocator. The platform sample separately tests owner threads,
wait/stop handoff and producer joins. The pure fixed-pool unit suite covers
alignment, bounds, overflow and metadata. Production identities need a suitable
fresh-instance source; the fixture source is intentionally deterministic.

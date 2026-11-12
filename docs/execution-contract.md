# Owner execution and shutdown

One owner supplies a monotonic tick and bounded budget to `wl_kernel_step`. Deadline processing precedes RX. Four outbound calls, four reply snapshots and one ordinary snapshot share a nine-index FIFO. Reserve response storage before handler side effects. BUSY retains encoded bytes and waits for real activity; an independently admitted duplicate may execute again.

An accepted call has one terminal callback and its handle then expires. Failure to admit has no future callback. Callback results are borrowed; copy before returning. Callback admission/cancel is allowed, recursive step/close is rejected. A logical terminal cannot release a physical USB/DMA borrow. Close the kernel, stop/join actual producers, process final completion and require quiescence before freeing storage. A fresh instance is mandatory on restart.

[Kernel API](static-endpoint-kernel-api.md) · [Development checks](development-current.md)

# Experimental physical UART polling

Enable `CONFIG_WIRELINK_ZEPHYR_UART_POLL_EXPERIMENTAL=y` (default off).
Initialize a COBS endpoint, attach with a caller-owned adapter and RX stack,
then start. The ordinary owner drives step/readiness/deadlines and user callbacks.
One RX worker reserves the existing SPSC ring, reads at most 64 bytes, commits
partial input, and notifies once per batch. Every iteration blocks on an
interruptible one-tick semaphore timeout. With 10 kHz ticks the request is 100 us;
relative timeout rounding and scheduling add delay. This is not a precise period.
The worker must be the only UART RX producer; disable other IRQ/DMA adapters.

TX runs synchronously on the owner via `uart_poll_out`. SENT means all bytes were
accepted and the source buffer is no longer referenced. It does not mean the
FIFO is empty, the peer accepted a message, or an RPC succeeded. Physical-drained
requests are rejected. This first version supports ordinary physical UART with
no hardware flow control. CDC, DMA and interrupt-driven RX are outside its scope.
Use UART hardware configuration appropriate to the deployment; do not share the
UART with a console or another protocol producer.

Cross-thread callers use `request_stop`: it latches stop and wakes worker/owner.
It does not close the endpoint. At an owner safe point, `stop` joins the worker
before detaching the sink; endpoint close invokes this quiesce automatically.
Never stop/join from the worker, notify callback, sink, or inside step. Join any
external request/notify callers before reattaching or releasing storage. Attach
only to a fresh/fully closed endpoint, using a fresh/fully stopped adapter. After
request_stop, call owner stop/close before restarting via attach/start. Repeated
stop is harmless. `uart_poll_out` must eventually return: a driver stuck waiting
for FIFO space cannot be cancelled through the generic Zephyr API. An interrupted
TX reports failure once and preserves the exact accepted-byte count.

RX backpressure leaves FIFO bytes unread and retries after sleeping; it is
counted separately from observed errors. `error_api_available` says whether the
UART exposes `err_check`; API availability does not establish that its driver
reads unmasked hardware error status. Unavailable or masked hardware overflow
counters are unknown, not observed zero. Observed errors enter the existing core recovery
contract. Corrupt bounded frames resynchronize at the next COBS delimiter. Noise
without a delimiter can fill the whole ring; the owner may explicitly use
`wl_feed_recover_reset` when appropriate, rather than treating a full ring as a
proved physical FIFO overrun or discarding queued valid frames automatically.

The experiment fixes cooperative RX priority -1, stack 2048 B, batch 64 and one tick. Its
extra worker and idle wake cost are part of the measurements. Owner priority,
clock and deadline rebasing remain the application's existing policy. Shared
ESP32 owner IRAM placement is enabled for both IRQ and polling; polling functions
have ordinary placement. Keep IRQ as the production control until a separate
replacement review establishes the required capabilities and resource limits.

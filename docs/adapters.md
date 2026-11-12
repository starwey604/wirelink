# Implementing and binding a transport

[中文](adapters-cn.md) · [Kernel API](kernel-api.md) · [Wire format](packet-protocol.md)

A transport integration supplies a packet sink, an RX producer, an owner-side
service/readiness path and a latched wake. Start with the connector that matches
your physical boundary:

| Transport | Connector | Boundary |
| --- | --- | --- |
| USB bulk, UDP, RAM loopback | native/NONE | One complete canonical DATA packet |
| UART / serial | COBS/CRC16 | Byte stream with CRC and zero delimiter |
| Length-delimited stream | length16/NONE | Two-byte length followed by canonical DATA |

Use a [profile-v2 binding](link-profiles.md) to generate the corresponding
route and storage. Framing, driver ownership and RPC completion are separate.

## Bind storage before starting I/O

1. Allocate a stable endpoint, C-owned packet port, two RX units, adapter and
   wake context. For streams also allocate connector TX scratch and any SPSC
   byte FIFO; DMA additionally needs its actual driver buffers.
2. Initialize the outer wake before any publication can invoke it. Use a fresh
   nonzero communication instance. Check all initialization results.
3. For native packets, bind `wl_packet_config_t.submit/notify/user` to the
   adapter's submit/notify functions and adapter context. Initialize the port,
   then bind/start the adapter in its documented order below.
4. For streams, port `submit` is the selected
   `wl_packet_stream_*_submit`, `notify` is `wl_packet_stream_notify` and `user`
   is the stream object. The stream config's `physical_submit`, `notify` and
   `user` point to the actual adapter and its wake. Initialize FIFO staging
   with a notifier that wakes the owner. Generated `*_kernel_transport_init`
   assembles the port/selected connector, not the driver or FIFO.
5. Initialize the generated endpoint with the port and handlers, then start
   input only after every referenced object is ready. Initialization itself
   must not dispatch application work or race active I/O.

The complete C-owned assembly is in
[`packet_tool_owner.c`](../examples/common/packet_tool_owner.c); the
[loopback example](../examples/bare_metal_loopback.c) is a smaller native path.
C++ adapters take opaque pointers/references to C atomic storage. Do not
substitute `std::atomic` or copy live port/FIFO objects.

## RX publishes complete packets, not callbacks

For native packets, one serialized producer claims writable RX storage,
receives/copies into it, then commits the actual nonzero packet length. Abort a
failed/empty receive instead of committing it. Validate the saved claim/token,
capacity and current driver request before changing ownership. A stale
completion must not abort or commit a newer claim. The owner alone borrows,
routes and releases RX; driver callbacks run no codec or application handler.

For a byte stream, the producer stages bytes in the C-owned FIFO; owner service
feeds the selected connector. Direct connector feed is also single-producer.
On pressure retain every unconsumed suffix and resume when storage is available.
Do not label full storage as byte loss. Actual overrun/discontinuity must be
reported as a gap and the partial parse aborted/resynchronized. COBS can drop
through the next delimiter; length16 needs an externally restored boundary
after arbitrary byte loss. See [stream interfaces](../include/wirelink/packet_stream.h)
and [FIFO interfaces](../include/wirelink/byte_fifo.h).

## TX reports source lifetime and the physical boundary

The sink runs synchronously on the owner. SENT means source consumed and
physical transfer complete **at this adapter's boundary**. STARTED accepts a
lease and promises token-bound completion, possibly inline before returning.
BUSY/FAILED accept nothing and cannot later complete that attempt. BUSY requires
an activity notification when resources return. A UDP send's boundary is local
socket acceptance, not delivery to the peer.

Publish CONSUMED when the source is no longer read, DRAINED when the accepted
transfer completes, or TX_ERROR when failure/cancellation ends ownership.
DRAINED implies CONSUMED; TX_ERROR implies both. Only owner take ends the
port's active flight. The kernel retains leased snapshots until DRAINED, even
after a logical timeout/cancel. The [port API](kernel-api.md#submit-physical-tx)
defines return values, flags and token validation in full.

A stream's physical source is its encoded TX scratch, not the original
canonical snapshot. Keep both stream and scratch alive until physical
completion/stop acknowledgement; preserve the original port token when
publishing completion. Queue-empty, callback counts and RPC terminal outcomes
cannot establish physical drain.

## Service, readiness and waiting belong to the owner

Call adapter service around kernel step as needed to parse staged input,
resume paused RX, publish deferred completion and retry resources. Merge core
readiness/deadline, adapter readiness/deadline and application work before
sleeping. `wl_io_readiness_t.next_deadline_ms` is a relative millisecond hint;
`UINT32_MAX` means no deadline. `blocked` is a status hint, not runnable work.

Use a latched wait/notify pair so activity arriving between query and wait is
not lost. Notify must be nonblocking and safe for its I/O/ISR callers. Do not
reenter adapter/kernel owner operations from it. Recompute finite remaining
wait after owner work. Kernel activity is a coalesced hint consumed by step;
it does not replace the external event. The
[host owner loop](../runtime/host/src/executor.cpp) implements this arrangement;
[Zephyr wait](../runtime/zephyr/include/wirelink/zephyr/wait.h) supplies a static
latched waiter without an internal thread.

## UART and host serial configuration

The UART paths use COBS/CRC16 plus FIFO staging. Do not share their RX producer
with another UART adapter or a console. Configure pins, flow control and buffer
placement for the actual board; platform cache hooks/attributes belong in that
platform setup, not the core.

### Zephyr IRQ

Enable `CONFIG_WIRELINK_ZEPHYR_UART_IRQ=y`; assemble
[`wl_kernel_uart_irq_config_t`](../adapters/zephyr/uart_irq/include/wirelink/zephyr/kernel_uart_irq.h)
with UART, port, FIFO and stream. Init requires the driver's genuine
`irq_tx_complete` capability, not just FIFO-empty interrupts. Missing capability
returns `WL_ERR_NOT_SUPPORTED`; the current CDC path is such a rejection case.

After FIFO acceptance consumes memory, TX interrupts are disabled and owner
service checks line idle. `drain_check_ms=0` selects `1 ms`; nonzero values
are bounded by `INT32_MAX` milliseconds. Readiness contributes that deadline
while drain is pending. A negative completion query is an error, not success.
Use `service`, `readiness` and `wait` with owner monotonic milliseconds.

`wl_kernel_uart_irq_stop` disables IRQ sources and detaches/synchronizes callbacks
under its binding lock, aborts the partial stream and publishes TX_ERROR for any
remaining transfer. This is cancellation, not a successful line-idle result.
Keep port/wake storage alive through final kernel completion processing.

### Zephyr DMA

Enable `CONFIG_WIRELINK_ZEPHYR_UART_DMA=y` and provide two disjoint DMA-capable
`rx_buffers` in
[`wl_kernel_uart_dma_config_t`](../adapters/zephyr/uart_dma/include/wirelink/zephyr/kernel_uart_dma.h).
These driver buffers are separate from the FIFO and packet-port RX units.
`timeout_us` and `tx_timeout_us` are driver microsecond timeouts, not RPC ticks;
negative values must be `SYS_FOREVER_US`. Cache preparation/completion hooks
must obey the driver's buffer ownership.

Use `wait_for_tx_idle=true` for a driver whose TX_DONE only establishes memory
consumption; owner service then queries `uart_irq_tx_complete` for actual drain.
Leave it false only when the driver's TX_DONE establishes the required physical
boundary. `drain_check_ms=0` selects `1 ms`; `tx_retry_ms=0` also selects a
bounded `1 ms` retry because rejected async UART submission has no generic
writable event. Both fields are at most `INT32_MAX` milliseconds. Merge these
deadlines from `wl_kernel_uart_dma_readiness`.

`stop` begins asynchronous shutdown. Continue `service` until stats have
`started=0` and `stopping=0`; RX-disabled alone is not RX-buffer-return.
`wl_kernel_uart_dma_quiesce` stops driver ownership, services required progress,
waits for dispatched callbacks, detaches the binding and aborts partial RX.
Only then, with kernel quiescence and outer notifier exit, release storage.

### Zephyr polling

Enable `CONFIG_WIRELINK_ZEPHYR_UART_POLL_EXPERIMENTAL=y`. The
[poll configuration](../adapters/zephyr/uart_poll/include/wirelink/zephyr/kernel_uart_poll.h)
requires caller-owned RX worker stack/priority and a prompt nonblocking
`tx_idle` callback. `uart_poll_out` consumes source; it does not prove line idle.
The callback returns positive when drained, zero pending, negative on error.
`drain_check_ms=0` selects `1 ms`, with nonzero values at most `INT32_MAX`.
Owner service/readiness drives checks; TX itself runs synchronously on the owner.

Request stop to wake the RX worker, then call owner-side `stop` to join it.
Stop can return `WL_ERR_WOULD_BLOCK` while physical TX is pending, or an error
if the idle query fails. Keep adapter/stack/port/stream storage and continue
shutdown until stop succeeds. Do not stop/join from worker, notifier or sink.
A stuck `uart_poll_out` has no generic cancellation API. See the
[polling guide](../adapters/zephyr/uart_poll/README.md).

### Astrial serial

Enable `WIRELINK_BUILD_ASTRIAL_ADAPTER=ON`, supplying the Astrial backend via
`WIRELINK_ASTRIAL_SOURCE_DIR`. Its
[`PacketSerialConfig`](../adapters/astrial/include/wirelink/astrial/packet_serial.hpp)
requires `drained` and a `drain_check_ms` in `1..INT32_MAX`, default `1 ms`;
zero is invalid here. `drained(drain_user)` returns positive/zero/negative for
physical drain/pending/error and must be nonblocking. The write callback only
publishes CONSUMED. Owner `service(now_ms)` and `readiness(now_ms)` complete
drain checks. `stop()` closes/joins the backend worker before aborting staging
and finalizing retained TX as needed. Automatic reconnect is disabled; rebuild
the stopped integration with fresh identity to restart.

## Native USB bulk configuration and recovery

Both native USB adapters require `maximum_frame_size` **strictly less** than
the packet RX unit/read capacity. This preserves room to distinguish a full
request from a legal frame ending at a short packet/ZLP. A successful empty
read is aborted without dispatch. Error/full-capacity reads are aborted and
latch a boundary/transfer fault, except expected shutdown cancellation. Stale
buffer/token callbacks cannot change current claims. Do not parse an uncertain
suffix as the next packet.

### Zephyr USB device

Enable `CONFIG_WIRELINK_ZEPHYR_USB_BULK=y` with the next USB device stack.
Initialize the port with `wl_kernel_usb_bulk_submit`,
`wl_kernel_usb_bulk_notify`, `user=adapter`, then call
[`wl_kernel_usb_bulk_init`](../adapters/zephyr/usb_bulk/include/wirelink/zephyr/kernel_usb_bulk.h)
before `usbd_init`/`usbd_enable`. The class binds one adapter. Use the endpoint
addresses assigned by the initialized descriptors, not assumed fixed addresses.
TX requests a ZLP for exact-MPS lengths; the actual UDC must preserve that
completion boundary. Owner service arms RX and reports faults.

`wl_kernel_usb_bulk_recover` is owner/thread-only, outside callbacks. It
requires a fault and a non-stopping adapter, disables/enables the entire USBD
context and requires host enumeration/configuration again. A successful call
starts recovery; the fault stays latched until configuration enables the class.
Repeated enable of a still-configured endpoint does not clear it. This operation
preserves the kernel instance and queued/leased work. A full endpoint teardown
and restart instead requires fresh identity.

For shutdown, `stop` disables USBD and can return `WL_ERR_WOULD_BLOCK` until
actual cancellation callbacks return buffers. Require `stopped`, keep stepping
to consume final port completion/RX, then `deinit`; deinit requires physical
acknowledgement and port quiescence. Keep the outer wake context alive until
callbacks have also exited. See the
[USB sample](../samples/zephyr/usb_bulk/README.md) for board/driver configuration.

### Astrial USB host

Enable `WIRELINK_BUILD_ASTRIAL_USB_ADAPTER=ON` with its backend and USB build
dependencies. Construct
[`PacketUsbAdapter`](../adapters/astrial_usb/include/wirelink/astrial/packet_usb.hpp)
before port init, bind its static submit/notify with `user=adapter`, then
`start(PacketUsbConfig)`. RX unit capacity must also be a multiple of the
endpoint's IN MPS; start validates nonzero IN/OUT MPS. One borrowed read is
outstanding, and automatic reconnect is disabled. The backend must implement
USB transfer boundaries and exact-MPS/ZLP correctly.

Faults are sticky. Stop joins borrowed read/write callbacks; explicitly reset
USB framing/reconfigure the device, then recreate the adapter/port with fresh
identity. Merely reopening the host object does not reset the remote boundary.

## UDP and loopback boundaries

Asio [`PacketUdpAdapter`](../adapters/asio_udp/include/wirelink/asio/packet_udp.hpp)
and Zephyr [`wl_kernel_udp_t`](../adapters/zephyr/udp/include/wirelink/zephyr/kernel_udp.h)
perform socket I/O on the owner. Their maximum datagram size must be strictly
less than each RX unit: the read uses an extra byte to reject over-limit input.
`service_budget` is `1..16` (Asio default `8`). Empty, oversized/truncated and
wrong-peer datagrams are discarded without stream resynchronization. Optional
`decode` performs a selected in-place framing conversion before canonical
commit; it is not wire-format autodetection.

Asio binds a local numeric address and uses explicit `set_peer`, or opt-in
`learn_peer` from a valid packet. Peer learning is not authentication. Writable
socket readiness wakes a BUSY TX. Zephyr uses configured numeric IPv4 local/peer
addresses and `retry_ms`, where `0` selects `1 ms` and values are at most
`INT32_MAX`; its clock and readiness/wait limits use milliseconds. Configure
network interfaces independently. Waits dispatch no business callbacks.

Close on the owner. For Asio, stop closes the socket and aborts its retained
idle RX claim. Join the waiting owner and every external notifier before
adapter destruction. For Zephyr, `request_stop` interrupts waits; after external
users/owner are synchronized, `close` releases sockets/eventfd. Keep notifier
storage alive until its producers have joined.

`wl_packet_loopback_t` retains source bytes until owner
[`wl_packet_loopback_service`](../src/packet_loopback.c) copies them to a peer
RX claim. Only then is DRAINED published. A full peer RX leaves the transfer
pending. Stop abandons actual pending transfers with TX_ERROR. Both directions
share one owner; it is a RAM model, not a concurrent physical link.

## Finish shutdown and validate the affected boundary

On the owner, close the kernel, stop actual producers, finish the adapter's
specific acknowledgement/join and keep stepping through logical terminals,
queued RX and final physical completion. Require kernel quiescence **and**
adapter silence before freeing endpoint, C atomics, staging, driver buffers,
worker stack or wake context. Restart only after that boundary, using a fresh
nonzero instance; counters/tokens do not wrap into old work.

The [development guide](development.md#map-a-change-to-decisive-checks) maps
adapter changes to `static_uart`, `static_usb_bulk`, `static_udp` and controlled
host fixtures. Exercise inline/delayed completion, pressure, stale tokens,
stop with retained buffers and fault recovery. Hardware qualification also
requires the actual driver/board and [HIL evidence](adapter-hil.md).

# Adapter Lifecycle

[Notifications, close and TX consumed/drained](execution-contract.md)

Wirelink adapters preserve one lifecycle even when their platform APIs differ:

1. Initialize `wl_ctx_t` and all caller-owned storage.
2. Initialize the adapter and bind its TX sink while the driver is stopped.
3. Activate RX or enable the platform transport.
4. On every RX, TX-completion, writable, or deadline wake, call `wl_poll()` and
   then adapter/runtime service from the single owner.
5. Stop producers, drain deferred completions, and quiesce before reusing link
   or adapter storage.

`WL_ERR_WOULD_BLOCK` is backpressure, not a terminal adapter failure. Wait for
the next transport notification and give the owner one more service pass.

## Platform Mapping

| Adapter | Envelope | Activate | Owner service | Quiesce |
| --- | --- | --- | --- | --- |
| Loopback | native packet | `wl_loopback_init()` | `wl_loopback_service()` | `wl_loopback_quiesce()` |
| Zephyr UART DMA | COBS stream | `wl_zephyr_uart_dma_start()` | `wl_zephyr_uart_dma_service()` | call `wl_zephyr_uart_dma_stop()`, then service until `started=0` and `stopping=0` |
| Zephyr UART IRQ / CDC ACM | COBS stream | `wl_zephyr_uart_irq_start()` | `wl_zephyr_uart_irq_service()` | disable the owning UART/USB device before link reuse |
| Zephyr USB bulk | native packet | enable the owning USBD context | `wl_zephyr_usb_bulk_service()` | disable USBD before link reuse |
| Astrial serial / USB | COBS stream / native packet | `start()` | `service()` and `wait_for_activity()` where available | `quiesce()` |
| Asio UDP | native packet; explicit legacy COBS | `open()` and socket bind; endpoint overload attaches hooks | `service()`; deadline-aware `wait_for_activity()` | `quiesce()` or endpoint close |
| Zephyr UDP (experimental) | native packet | `wl_zephyr_udp_open()` attaches to endpoint | generated step services RX; `wl_zephyr_udp_wait()` | generated endpoint close, join notification producers, then `wl_zephyr_udp_close()` |

See the [Zephyr UDP contract](zephyr-udp.md) for static storage, backpressure
timing and the known native-stack allocation wait. Ordinary generated endpoint
users do not manually invoke adapter service in addition to step.

UART DMA stop waits for actual RX buffer returns and TX completion. Once the
driver returns its buffers, stop discards unpublished staging bytes; it does
not require the closing owner to drain the core RX ring. Quiesce also waits for
entered callbacks to exit. An owner restarting a stopped receive stream is
responsible for clearing or resynchronizing its buffered input.

ESP32 UART IRQ builds place the IRQ and owner execution bridge in IRAM. The
owner bridge uses the Espressif linker support for `RAMFUNC_SECTION`; the tested
Zephyr revision is recorded in the [execution report](execution-refactor-round-1-result-cn.md).
The ESP32-S3 build uses approximately 2.8 KiB more IRAM for this placement.

`CONFIG_WIRELINK_ZEPHYR_UART_IRQ_TIMING` optionally counts valid UART callback
entries and elapsed cycles inside each callback. `callback_irqs` counts entries;
`rx_irqs` and `tx_irqs` count ready-source processing attempts. `callback_ticks`
excludes the driver ISR and callback lock acquisition/release. The counters use
32-bit wraparound and `k_cycle_get_32()`. Disabled builds add no timing reads,
storage or ISR work. Enabled builds add fields to the adapter and stats layouts;
build the application and library with the same configuration.

ESP32 short-frame latency also depends on the driver RX timeout. The
[local Zephyr patch](../adapters/zephyr/patches/README.md) adds
`CONFIG_UART_ESP32_RX_TIMEOUT`, retaining the upstream default of 22 character
periods. It applies to every UART instance using this driver; the Wirelink core
does not configure hardware timeouts. See the separate
[22/8/4/2 timeout measurements](uart-rx-timeout-optimization-cn.md).

The Asio UDP adapter permits the peer to bind later or close first. On Windows it
disables `SIO_UDP_CONNRESET` reporting: an ICMP Port Unreachable from a previous
datagram does not terminate reception. Reliable/RPC deadlines still detect an
absent peer; successful UDP send does not promise delivery. See
[Winsock IOCTLs](https://learn.microsoft.com/en-us/windows/win32/winsock/winsock-ioctls).

With a full RX queue or retained RX lease, Asio UDP waits for a latched
notification or its deadline. Socket RX waits require receive capacity;
cancellation drains both socket and timer handlers before the
waiter's local state leaves scope. `WL_NO_DEADLINE` selects an indefinite wait.

Typed adapters expose platform-specific configuration and statistics; the C
core does not add virtual dispatch or own their wait primitives.

## Owner-Loop Recipe

Map `service` and `quiesce` into `wl_pump_hooks_t`. Map adapter and generated
runtime deadline hints separately, then use `wl_pump_get_readiness()` to choose the
next wait. A wake always runs one `wl_pump_step()` before consulting the next
hint, so sink backpressure cannot become a zero-delay busy loop.

Keep the query timestamp when there is work between querying and waiting.
For endpoints, sample `wl_endpoint_now`, query `wl_endpoint_get_readiness_at`,
then use `wl_wait_remaining` with a fresh sample at waiter entry. Take the minimum
with the caller's business limit in the same clock domain. Raw/manual owners can
use the same helper with the timestamp passed to `wl_pump_get_readiness`.
See [execution and waiting](execution-contract.md) for latch acknowledgement,
external queues, partial RX, stop and finite-timeout rules.

The USB CDC IRQ sample queries the endpoint at a saved endpoint-clock snapshot,
rebases finite hints with `wl_wait_remaining`, and calls the waiter returned by
`wl_endpoint_waiter`. Runnable or expired work uses a zero-time wait, which also
observes stop without blocking. The sample consumes notifications only in wait;
a wake always leads to the next bounded endpoint step. Its small owner helper is
sample-local, not an adapter API.

`wl_zephyr_uart_irq_wait_ready` has been removed without a compatibility wrapper.
Raw/manual `wl_zephyr_uart_irq_wait` still accepts caller-owned kernel timeouts;
sub-millisecond users can retain the existing Zephyr timeout helpers. Attach,
waiter descriptors, notifications and stop keep their existing responsibilities.

Choose exactly one ingress path per link: `wl_feed()` for copied stream input,
reserve/commit or DMA claims for direct stream input, and the unit queue for
native packets. Never mix ingress families on one initialized context.


## USB Bulk configuration and RX preparation

Use `<wirelink/usb_bulk_defaults.h>` and link `Wirelink::usb_bulk_defaults`.
Both platform adapters propagate this dependency. USB Bulk uses only native_packet. Select one
`wl_usb_bulk_config_t` and reuse it before link initialization, during explicit
RX preparation, and when binding the adapter:

```c
/* Application budget example, not a library-wide default. */
enum { APP_USB_RX_CAPACITY = 2304, APP_USB_RX_SLOTS = 3 };
static uint8_t rx_storage[APP_USB_RX_SLOTS][APP_USB_RX_CAPACITY];
const wl_usb_bulk_config_t usb =
    wl_usb_bulk_native_packet(APP_USB_RX_CAPACITY, APP_USB_RX_SLOTS);
int err = task_endpoint_config_defaults(&config, environment, wl_usb_bulk_link_defaults());
if (err != WL_OK) return err;
err = wl_usb_bulk_config_apply(&config.link, usb);
if (err != WL_OK) return err;
err = task_endpoint_init_config(&endpoint, &config);
if (err != WL_OK) return err;
wl_ctx_t *link = wl_endpoint_link(task_endpoint_handle(&endpoint));
err = wl_usb_bulk_rx_init(link, usb, &rx_storage[0][0], sizeof(rx_storage));
if (err != WL_OK) return err;
/* Pass usb as adapter_config.transport on either platform. */
```

The sole factory selects native_packet and NONE together. The application
supplies capacity and 2..8 slots; 2048×2 is an experiment setting. The generated
profile must support native layout. Both peers explicitly agree on envelope
and integrity. Upgrade both peers together: old USB COBS firmware is wire
incompatible. USB Bulk no longer has an envelope selector, byte ring or stream
staging path; there is no negotiation or fallback.
Override the USB value's integrity before applying it when CRC16/CRC32C is
required. The old `wl_usb_bulk_link_defaults()` remains the integrity-only NONE
value, including for CDC integrations.

Applying the USB selection changes only the pre-init envelope/integrity and
preserves payload/session/MTU/retry settings. Native capacity must be strictly
greater than `wl_frame_raw_size(max_payload_len, integrity)`, including the
largest header. Equality, invalid slots, overflow and invalid selectors fail
without modifying configuration. Final link/profile capability checks still
apply; unsupported modes never fall back.

Prepare caller-owned native storage once, before binding. Repeated preparation
fails rather than resetting queued frames, claims or event leases. Keep storage
alive until the adapter is quiesced, leases are returned and the link is closed.
The default-constructed Astrial transport has no implicit memory budget.
Adapters only validate wire settings and queue geometry; they never create
or replace that queue. Replace the old maximum_read_size/maximum_rx_size,
unit_queue_slots and Zephyr storage fields with `transport` and explicit RX
preparation. Generic generated SDKs remain independent of USB and OS headers.

## Observable RX faults and recovery

Native frames end with a short packet or explicit ZLP. Empty successful
completions return claims without an event or error. Failed/full transfers or
failed queue commits abort the claim and latch RX stopped with a specific
`WL_USB_BULK_RX_FAULT_*` reason. Backpressure resumes after event release;
a boundary fault requires transport recovery.

Faults wake the owner and make readiness runnable for a service pass.
`service()` returns WL_ERR_IO so endpoint/pump callers can observe failure.
After notification is consumed, a permanent fault does not cause a readiness
busy loop. Explicit service calls still report an unrecovered fault. Query
Astrial `rx_fault()` or platform stats; Zephyr also provides
`wl_zephyr_usb_bulk_rx_fault()`.

From a Zephyr owner thread, call `wl_zephyr_usb_bulk_recover()`. It disables and
enables the whole USBD context, affecting other USB classes on that context,
and waits asynchronously for host enumeration/configuration. Never call from
ISR or USB callbacks. Success means recovery requested; stats.recovering stays
true and the fault remains until actual configuration. Repeated requests return
WL_ERR_WOULD_BLOCK. Failures preserve the fault; an enable failure can be retried
without repeating a successful disable. A duplicate enable callback alone
cannot clear a fault. A rapid reconfiguration preserves any unconsumed IO
notification and last_rx_fault, even if the current rx_fault is already NONE.
Resetting statistics does not clear recovery state or pending notifications.
The Zephyr Bulk sample demonstrates application reporting and recovery.

On Astrial, record the fault, quiesce from the owner, reset/reconfigure USB or
physically reconnect, then explicitly close/rebuild the endpoint, prepare RX
and reopen. The peer can request reconfiguration through the Zephyr API above.
Reopening the host adapter alone, clearing counters, or resetting the core
parser does not flush the device's unread suffix. Neither recovery path silently
discards leased/queued frames or changes core/RPC sessions. Applications manage
transaction cancellation and new sessions explicitly. USB Bulk accepts only
native packets; UART and CDC retain their own byte-stream contracts.

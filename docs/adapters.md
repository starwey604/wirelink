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
| Zephyr USB bulk | native packet or COBS stream | enable the owning USBD context | `wl_zephyr_usb_bulk_service()` | disable USBD before link reuse |
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


## Select USB defaults before initialization

For native USB Bulk, one frame must end with a short packet or an explicit
ZLP. Both peers must explicitly select `native_packet`; COBS and native are
wire incompatible. The native read request must be strictly larger than
`wl_frame_raw_size(max_payload_len, integrity)`, including the largest frame
header. Provide at least two unit slots and retain their storage for the
adapter's lifetime. A successful empty completion returns its claim without
publishing an event or recording an IO error.

A failed or full native request loses the delimiter guarantee: RX stops,
the claim is aborted and an error remains recorded. Reset/reconfigure the USB
transport before enabling Zephyr USBD again or reopening the Astrial adapter
with a reinitialized link. A host adapter reopen alone does not flush a
device's unread suffix. Ordinary queue backpressure resumes after event
release; a boundary fault does not. Cancellation during shutdown returns the
claim without publishing partial data.

The generated config factory and quick init now require a third argument:
`wl_link_defaults()` selects CRC16; `wl_usb_bulk_link_defaults()` from
`<wirelink/usb_bulk_defaults.h>` selects NONE. Generic generated SDKs depend
only on the portable C defaults header. CMake consumers of the USB header link
`Wirelink::usb_bulk_defaults`; the Astrial USB target and Zephyr module supply
its include path automatically.

```c
#include <wirelink/usb_bulk_defaults.h>

int err = task_endpoint_config_defaults(
    &config, environment, wl_usb_bulk_link_defaults());
if (err != WL_OK) return err;
/* Optional expert override: config.link.integrity = WL_INTEGRITY_CRC32C; */
return task_endpoint_init_config(&endpoint, &config);
```

Quick init accepts the same defaults value. `create()` consumes the final config.
Raw core users call `wl_config_apply_link_defaults(&config,
wl_usb_bulk_link_defaults())`; only integrity changes. Apply defaults, apply
user overrides, initialize, then bind the adapter. Binding never rewrites the
configuration. Both peers must agree. Invalid selectors fail, and fixed
`cobs-crc16` rejects NONE with `WL_ERR_NOT_SUPPORTED` without fallback.

The preset preserves the profile envelope, payload/session/MTU/retry settings,
length checks, RPC deadlines, replay and business validation. The CDC IRQ sample
explicitly opts into the same trusted direct USB defaults, matching the host
CDC tool; the generic UART IRQ adapter does not infer the medium. Ordinary UART
uses the generic CRC16 defaults. The Bulk example already used NONE before this
change, so this API change makes no new performance claim.

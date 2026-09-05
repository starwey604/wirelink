# Adapter Lifecycle

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
| Asio UDP | native packet | construction and socket bind | `service()` | `quiesce()` |

Typed adapters expose platform-specific configuration and statistics; the C
core does not add virtual dispatch or own their wait primitives.

## Owner-Loop Recipe

Map `service` and `quiesce` into `wl_pump_hooks_t`. Map adapter and generated
runtime deadline hints separately, then use `wl_pump_get_hint()` to choose the
next wait. A wake always runs one `wl_pump_step()` before consulting the next
hint, so sink backpressure cannot become a zero-delay busy loop.

Choose exactly one ingress path per link: `wl_feed()` for copied stream input,
reserve/commit or DMA claims for direct stream input, and the unit queue for
native packets. Never mix ingress families on one initialized context.

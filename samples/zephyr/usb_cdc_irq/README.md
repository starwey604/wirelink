# CDC IRQ capability probe

The former CDC echo endpoint is retired. The current Zephyr next-stack CDC ACM
UART API has FIFO fill and TX-ready functions, but no `irq_tx_complete` function.
Accepting bytes into a FIFO does not prove that a USB transfer has drained.
The packet UART IRQ adapter explicitly returns `WL_ERR_NOT_SUPPORTED` for this
API, as required by the physical TX ownership contract.

This sample now probes that capability on the actual CDC device. It starts no
USB enumeration or physical traffic and does not claim a working CDC transport.
If a future driver exposes the missing API, the probe reports that physical
drain semantics still need qualification. Use `samples/zephyr/usb_bulk` for the
supported USB packet transport. No UART FIFO-empty fallback is provided.

Twister builds nRF52840 DK only. Simulated missing-completion rejection and
physical UART completion/stop behavior run in
`tests/zephyr/integration/static_uart`. This does not constitute a hardware CDC
test. The local diagnostic identity is never placed on wire.

# UART DMA packet logger

This diagnostic sample logs message IDs and lengths of ordinary opaque DATA
payloads up to 256 bytes. It uses the default four-call/four-reply C kernel,
explicit packet RX units, a COBS/CRC16 stream connector, a byte staging FIFO and
two caller-owned DMA buffers. It registers no RPC service and sends no replies.
The old ARQ/CRC32C stream configuration is removed; peers must explicitly use
COBS/CRC16 ordinary DATA. There is no automatic wire-profile negotiation.

The owner obtains a fresh identity from the native platform before initialization,
reads the clock outside the kernel, merges core and driver deadlines, rebases
elapsed time before waiting, and uses the adapter's latched wake. ISR callbacks
stage bytes only. The receive callback retains no frame pointer. Shutdown joins
actual DMA buffer returns and dispatched callbacks before storage can be reused.

Twister builds the ESP32-S3 UART1 overlay. This build does not qualify a physical
UART cable, DMA coherency on another SoC, or its TX-idle implementation. Actual
UART software contracts (buffer return, backpressure, gap recovery, physical
completion and stop) run in `tests/zephyr/integration/static_uart`.

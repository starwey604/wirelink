# Experimental physical UART polling

Enable `CONFIG_WIRELINK_ZEPHYR_UART_POLL_EXPERIMENTAL=y`. The application owns
adapter storage, the RX thread stack, byte FIFO, selected COBS/CRC16 connector,
packet port and generated endpoint. A single RX worker stages up to 64 bytes
per pass and sleeps one interruptible tick. Owner service parses and dispatches;
no codec or user callback runs on the RX worker.

`uart_poll_out` establishes source consumption. A required platform `tx_idle`
provider confirms physical drain before the next transfer; it must be prompt
and nonblocking. Neither phase proves peer receipt or RPC success. Configure
real UART flow control and pin ownership in the application, and do not share
this RX producer with a console, IRQ or DMA adapter.

Request stop to wake the worker, then perform owner-side stop and join. It can
remain pending until hardware drain is confirmed. Consume final packet-port
completion and release RX borrows before endpoint quiescence and reinit; use a
fresh instance identity. Never stop/join from the worker or a callback. A stuck
`uart_poll_out` cannot be cancelled by the generic Zephyr API.

FIFO pressure leaves hardware bytes unread; actual receive errors mark a gap
and COBS resynchronizes at a delimiter. Missing or masked driver error counters
remain unknown. This path was tested with the real adapter and a controlled UART
API on native_sim, Cortex-M3 and RISC-V; USB results and software fixtures do not
establish UART line performance. No new kernel/owner IRAM placement is added.

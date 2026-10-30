# Packet adapter lifecycle

[中文](adapters-cn.md) · [Kernel ownership](static-endpoint-kernel-api.md).

Initialize C-owned port/RX storage with a fresh instance, bind the selected
adapter, then start I/O. A serialized producer publishes only input/completion;
the single owner services the adapter and kernel. There is no old ctx, event
pump or implicit generated adapter service. Combine readiness, core/application
deadlines and latched activity before waiting; rebase finite timeouts.

| Adapter | Connector | Physical completion / shutdown |
| --- | --- | --- |
| `wl_packet_loopback_t` | Canonical native/NONE | One owner copies to peer storage before consuming/draining; stop abandons actual retained transfer |
| Zephyr `wl_kernel_uart_dma_t` | COBS/CRC16 + C-owned FIFO | Driver TX done and actual RX buffer return; stop/service/quiesce synchronize callbacks |
| Zephyr `wl_kernel_uart_irq_t` | COBS/CRC16 + FIFO | Requires actual `irq_tx_complete`; unsupported CDC capability is rejected |
| Zephyr `wl_kernel_uart_poll_t` | COBS/CRC16 + FIFO | Explicit physical transmitter-idle query; pressure differs from byte loss |
| Zephyr `wl_kernel_usb_bulk_t` | Native/NONE | USBD callbacks, token/MPS/ZLP checks, asynchronous stop acknowledgement and deinit |
| Astrial `PacketUsbAdapter` | Native/NONE | Real borrowed read/write callbacks; stop closes and joins backend before storage release |
| Astrial `PacketSerialAdapter` | COBS/CRC16 + FIFO | Write callback consumes source; required nonblocking physical drain query completes TX; stop joins worker |
| Asio `PacketUdpAdapter` | Native/NONE datagrams | Owner-only socket I/O; latched interrupt for waits; caller joins owner/notify producers before destruction |
| Zephyr `wl_kernel_udp_t` | Fixed-peer native/NONE | Owner-only socket I/O; stop/wait notification, task join and socket close |

SENT means actual source consumption and physical completion at that adapter's
boundary. STARTED retains the source, may complete inline, and publishes token-
bound CONSUMED/DRAINED or TX_ERROR. BUSY/FAILED accept nothing and cannot later
complete. BUSY must notify when a resource becomes available. A logical RPC
terminal never permits overwriting bytes still borrowed by a driver.

Close the core, stop actual producers and continue owner processing until
physical cancellation/completion and kernel/port quiescence. Keep callbacks,
port, staging and wake contexts alive until actual callback/worker exit. A
stopped flag alone is not proof of driver return. Reinitialize only with a fresh
communication instance. Tokens and operation counters cannot wrap into old work.

USB read capacity must be strictly greater than a legal frame. Empty successful
reads return without dispatch; full/error/truncated reads abort and latch a
boundary fault. A fault requires actual USB boundary reset/reconfiguration;
reopen alone does not recover it. Stale completions cannot affect a fresh claim.
Preserve exact-MPS/ZLP handling. UART stream loss requires explicit window
abort/resynchronization, while ordinary pressure pauses/resumes without losing
bytes. See installed adapter headers and [HIL scope](adapter-hil.md).

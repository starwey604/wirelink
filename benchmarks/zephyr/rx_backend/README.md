# RX ring backend benchmark (ESP32-S3)

[中文](README-cn.md).

`rx_backend` measures Wirelink's receive byte path on real hardware: the atomic
SPSC BipBuffer (`src/rx_ring_bipbuf.c`), the `wl_rx_reserve` / `wl_rx_commit` /
`wl_feed_bytes` primitives, and the Zephyr async-UART DMA adapter
(`adapters/zephyr/uart_dma`). Build and trace it like the other firmware
benchmarks in [../README.md](../README.md); this guide covers only what is
specific to this app.

The app is a hardware fixture, not a timing gate. Twister builds it for all
ingress modes but never runs it; `native_sim` and QEMU remain correctness gates,
not clocks.

## Ingress modes

`WIRELINK_BENCH_INGRESS` selects how bytes reach the RX ring:

| Value | Producer |
| --- | --- |
| `IRQ` (default) | UART1 RX interrupt reads the FIFO in at most `BENCH_UART_CHUNK` (64) bytes and calls `wl_feed_bytes` |
| `DMA` | The Zephyr async UART DMA adapter owns UART1 RX; the main loop services the adapter and polls the link |
| `USB` | CDC ACM interrupt ingress; needs `EXTRA_CONF_FILE=usb.conf` and a host that sends frames |

UART0 TX generates the traffic in the non-USB modes. It only fills the FIFO in
bounded chunks without a TX interrupt. ESP32-S3 exposes one UHCI0 engine, so
UART0 async TX and UART1 async RX cannot run at the same time; the source UART
stays on plain FIFO writes for that reason.

## Workloads

`WIRELINK_BENCH_STREAM` selects the workload:

- **Stop-and-wait** (default): send one frame, wait for its delivery, record the
  per-frame latency, then repeat. Emits `wirelink_rx_bench_v1` rows. The line is
  idle between frames.
- **Continuous stream** (`=ON`): keep the TX FIFO full and drain RX
  concurrently, so the line is continuously busy and the adapter's re-arm path
  runs under sustained load. Emits `wirelink_rx_stream_v1` rows.

Both workloads run first the UART-free `wirelink_rx_primitive_v1` profiles
(reserve/commit and feed microbenchmarks) so the ring itself is measured
independently of any ingress. The stream workload is not available in `USB`
mode.

## Build and flash

```sh
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/benchmarks/zephyr/rx_backend -d build/rx-hw -- \
  -DZEPHYR_EXTRA_MODULES=/path/to/wirelink \
  -DWIRELINK_BENCH_INGRESS=DMA -DWIRELINK_BENCH_STREAM=ON
west flash -d build/rx-hw --esp-device /dev/ttyACM0
python benchmarks/zephyr/capture_serial.py --port /dev/ttyACM0 \
  --out stream.log --until "wirelink_rx_bench_v1,error" --seconds 180
```

The console is the on-board USB Serial/JTAG device (`/dev/ttyACM0` on Linux).
The UART bridge is a separate port; see [../README.md](../README.md) for the
board-specific port layout and the udev rule.

CMake options, all optional:

| Option | Effect |
| --- | --- |
| `WIRELINK_BENCH_STREAM=ON` | Continuous-stream workload instead of stop-and-wait |
| `WIRELINK_BENCH_RING=N` | Override the usable RX ring size (default 4096) |
| `WIRELINK_BENCH_DMA_CHUNK=N` | Override the maximum direct DMA claim (default = ring) |
| `WIRELINK_BENCH_STREAM_FRAMES=N` | Measured frames per stream profile (default 2000) |
| `WIRELINK_BENCH_PAYLOAD_START=I` | First payload profile index (default 0: 16 B) |
| `WIRELINK_BENCH_PAYLOAD_END=I` | One past the last payload profile index (default all) |

The payload profiles are 16, 20, 64, 120, 256, 1024 and 2048 bytes. The `START` /
`END` selectors are the way to keep a smoke run short. Additional `BENCH_*`
macros (warmup, frame timeout, UART idle timeout, chunk size) can be set through
`-DCMAKE_C_FLAGS=-DBENCH_...`.

## Output

`wirelink_rx_bench_v1,meta,...` records the clock, ring size, physical ring size
and fallback size. A header line then one row per payload for the selected
workload.

`wirelink_rx_stream_v1` columns:

| Column | Meaning |
| --- | --- |
| `wire_bytes` | Wire bytes sent (all frames, including warmup) |
| `received` | Frames delivered and payload-verified |
| `run_cycles` | First frame queued to last valid delivery, in hardware cycles |
| `producer_calls` / `producer_cycles` | IRQ: `wl_feed_bytes` calls and their CPU cycles. DMA: adapter RX-ready events and publisher cycles |
| `accepted` | Bytes accepted into the ring |
| `dropped` / `overflow` / `malformed` / `bad_integrity` | Ingress and link RX counters |
| `adapter_errors` | DMA adapter errors (DMA ingress only) |
| `latency_*` | Median / p95 / p99 / max of queue-to-delivery, measured frames only |

`run_cycles` is derived from the millisecond uptime, because the 32-bit ESP32
hardware counter wraps inside a long stream. Per-frame latency still uses the
hardware counter.

## Fixture and comparison

- ESP32-S3-DevKitC, `esp32s3_devkitc/esp32s3/procpu`, 240 MHz.
- UART0 TX GPIO16 wired to UART1 RX GPIO18, 3 Mbaud 8-N-1, no flow control.
- The camera fixture wires nothing else; do not connect UART0 RX or UART1 TX.

Freeze the ELF, `.config` and SHA-256 before changing the RX path, build the
candidate in another directory, and compare the same payload set, ring and claim
sizes, and UART fixture.

## What it does not measure

- No flow control, no deliberate backpressure stall, and no adversarial frame
  corruption; the default stream workload only overloads the consumer through
  the natural producer/consumer rate difference.
- USB data ingress is build-only here and cannot be compared with the UART
  numbers.
- Only the ESP32-S3 UHCI0 shape is covered. Platforms with independent UART DMA
  engines can overlap TX and RX and are not represented.
- The numbers are fixture- and revision-specific; they are not wire-latency or
  portability claims.

## Continuous-stream status

Continuous streaming exposes a path the stop-and-wait workload cannot see. On
the current adapter, `IRQ` ingress completes every payload, while `DMA` ingress
with the default finite idle timeout starts losing frames as soon as the first
full 4096-byte claim is published, and the loss grows with the payload size. The
cause is the single-claim finite-timeout mode plus a direct claim as large as the
ring: the producer cannot publish a full claim while the consumer still holds
unread data. Treat a `received` lower than the frame count as a failure, not as
noise. The workload is retained so the multi-claim / continuous-path fix can be
verified against it; see [../../../docs/rx-performance.md](../../../docs/rx-performance.md)
for the RX buffer history.

# RX SPSC buffer selection and performance record

Wirelink now has one RX byte-buffer implementation: its atomic SPSC
BipBuffer (`src/rx_ring_bipbuf.c`). This document records the comparison that
led to that decision and the procedure retained for future regression checks.

## Selection

The RX buffer is on the hot path between one ISR or DMA-completion producer and
one main-loop consumer. It must provide an acquire/release SPSC hand-off, a
continuous producer reservation, a continuous consumer view for in-place COBS
decoding, and must retain borrowed frame bytes until `wl_event_release()`.

The custom BipBuffer is the fixed implementation because it meets those
requirements while using all caller-provided storage and giving the producer
and consumer contiguous spans without a wrapper or an extra capacity byte. Its
cursor ownership and C11 atomics are explicit: the producer release-publishes
the write cursor after writing data; the consumer acquire-loads it, and uses the
matching ordering when returning space. Initialization rejects targets where
the required atomics are not lock-free.

MaJerle/LwRB v3.3.0 was evaluated as an alternative. It also supplied SPSC
operations and continuous spans, but required 4097 physical bytes for a 4096
byte usable ring and added a vendored dependency and a selectable-backend
surface. On the measured UART producer paths it was materially slower. The
backend switch, its Kconfig/CMake choices, test variants, and vendored source
were therefore removed. This is a deliberate fixed implementation, not a
promise of runtime or build-time RX backend interchangeability.

The decision concerns the byte buffer only. A frame that crosses the physical
end still takes Wirelink's documented fallback-copy path; a contiguous COBS
frame is decoded in place and its payload remains borrowed until release.

## ESP32-S3 UART comparison — 2026-08-16

This is the decision dataset, collected before LwRB was removed at source
revision `880fa9e` (`bench: use esp32 cycle counter`). Each of the four images
was flashed and measured once. It is sufficient to distinguish the observed
large producer-cost difference, but it is not a five-reset statistical study;
repeat the procedure below after a material RX-path change.

Fixture and image settings:

- ESP32-S3-DevKitC-1, Zephyr target
  `esp32s3_devkitc/esp32s3/procpu`, CPU fixed at 240 MHz.
- UART1 loopback: GPIO17 (TX) wired directly to GPIO18 (RX), 3 Mbaud, 8-N-1,
  no flow control. UART0 carried the serial CSV output.
- Logging, Wi-Fi, Bluetooth, power management, and dynamic frequency changes
  were disabled in the measured image.
- A 4096-byte usable ring and 2084-byte fallback buffer were used. LwRB's
  historical comparison image received 4097 physical bytes because it reserves
  one byte to distinguish full from empty.
- The UART profile uses 1,000 warm-up frames then 10,000 measured frames. The
  CPU hardware cycle counter measures producer calls and end-to-end latency.

At the largest tested payload (2048 bytes), all four images completed with
zero dropped bytes and no terminal ingress error:

| Buffer | Ingress | Producer cycles | Accepted bytes | Cycles/byte | Median latency | p95 latency | Max latency |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| BipBuffer | UART IRQ | 603,811,292 | 20,830,312 | 28.99 | 1,777,214 | 1,778,410 | 1,779,775 |
| LwRB (historical) | UART IRQ | 1,075,199,097 | 20,830,312 | 51.62 | 1,853,744 | 1,854,970 | 1,857,033 |
| BipBuffer | UART DMA | 27,615,292 | 20,830,312 | 1.326 | 1,880,331 | 1,884,017 | 1,887,307 |
| LwRB (historical) | UART DMA | 61,054,189 | 20,830,312 | 2.931 | 1,925,594 | 1,929,680 | 1,933,529 |

Relative to the historical LwRB image, BipBuffer used 43.8% fewer producer
cycles per byte in IRQ ingress and 54.8% fewer in the current DMA ingress.
The DMA numbers do **not** represent the current staged-DMA architecture. That
historical benchmark waited to commit and re-arm each 64-byte-or-shorter
transfer. The Zephyr adapter now owns two ping-pong DMA buffers and copies
completed prefixes into the SPSC ring; the core direct-DMA API remains a
single-claim path for controllers that do not require look-ahead buffers.

The primitive profiles below used 10,000 warm-up operations and 100,000
measured operations. `reserve/commit` omits the simulated producer's writes;
`feed` includes Wirelink's copy. Values are measured cycles, with the actual
number of reserve calls in parentheses.

| Chunk | BipBuffer reserve/commit | LwRB reserve/commit (historical) | BipBuffer feed | LwRB feed (historical) |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 15,454,618 (100,000) | 23,799,976 (100,000) | 21,777,088 | 30,133,037 |
| 8 | 15,450,832 (100,000) | 23,842,970 (100,171) | 22,357,816 | 33,148,576 |
| 32 | 15,452,501 (100,000) | 23,980,894 (100,757) | 23,750,470 | 35,002,967 |
| 64 | 15,690,766 (101,562) | 24,163,832 (101,538) | 26,009,188 | 37,879,143 |
| 256 | 16,413,227 (106,250) | 25,259,373 (106,224) | 38,779,578 | 54,673,654 |

USB CDC was intentionally not measured in this round and is not a decision
input. It remains a build-only benchmark ingress fixture; its endpoint buffer
would require a CDC-to-ring copy, so even future USB results must not override
the UART SPSC-buffer decision.

## ESP32-S3 independent-UART DMA validation — 2026-08-30

This round closes the same-UART fixture limitation above and validates the
Zephyr async-UART RX adapter over a sustained physical UART path.

Fixture and image settings:

- ESP32-S3-N8R2 revision 0.2, Zephyr
  `v4.4.0-11610-gbd8c15382376`, 240 MHz.
- UART0 TX on GPIO16 was wired directly to UART1 RX on GPIO18. Both UARTs ran
  at 3 Mbaud, 8-N-1, without flow control. The native USB Serial/JTAG device on
  `/dev/ttyACM0` carried benchmark output independently of both data UARTs.
- UART0 is only the traffic generator. The main thread fills its FIFO in
  chunks of at most 64 bytes and waits for TX idle; it does not enable a TX
  interrupt. UART1 is the measured Zephyr async RX/DMA path.
- The RX side used a 4096-byte ring, a 4096-byte maximum direct claim, and a
  200 us finite idle timeout. Finite-timeout mode deliberately uses one direct
  claim at a time, finishes the short claim in consumer context, then re-arms
  RX.
- Every payload used 100 warm-up frames followed by 1,000 measured frames.
  The complete run therefore transmitted 7,700 valid frames. Producer cycles
  cover the direct-prefix publication and adapter accounting performed by
  `UART_RX_RDY`; latency spans TX start through delivery by `wl_poll()`.

The sustained run completed every payload with exact payload validation, zero
dropped bytes, and no adapter error:

| Payload | Frames | Accepted bytes | Producer cycles | Median cycles | Median | p95 cycles | p99 cycles | Max cycles |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 1,000 | 44,000 | 149,000 | 59,917 | 249.7 us | 59,963 | 60,230 | 62,828 |
| 20 | 1,000 | 48,000 | 149,000 | 63,304 | 263.8 us | 63,351 | 63,524 | 65,994 |
| 64 | 1,000 | 92,000 | 149,000 | 100,393 | 418.3 us | 100,473 | 100,521 | 103,136 |
| 120 | 1,000 | 148,000 | 149,000 | 147,420 | 614.3 us | 147,466 | 149,929 | 150,104 |
| 256 | 1,000 | 284,032 | 149,000 | 263,079 | 1.096 ms | 265,272 | 266,432 | 268,727 |
| 1024 | 1,000 | 1,055,031 | 149,000 | 918,899 | 3.829 ms | 918,937 | 919,567 | 919,602 |
| 2048 | 1,000 | 2,083,031 | 149,000 | 1,789,397 | 7.456 ms | 1,789,430 | 1,790,339 | 1,790,372 |

At 149 cycles per completion, the measured producer-side adapter work is about
0.62 us per frame at 240 MHz. Most end-to-end time is UART serialization and
the 200 us idle boundary; the tight p95/p99 spread shows that the DMA release,
consumer finish, and re-arm cycle remains deterministic over the sustained
run. The accepted-byte variation at larger payloads is expected because the
COBS wire length depends on frame contents.

ESP32-S3 exposes one UHCI0 engine to the UART async driver. UART0 async DMA TX
and UART1 async DMA RX therefore cannot operate concurrently: attaching UHCI0
to the source UART disconnects the measured RX path. The independent source
uses bounded FIFO writes for that reason. This is a fixture constraint, not a
Wirelink adapter requirement on platforms with independent UART DMA engines.

A rapid USB-JTAG reset loop is also not accepted as the five-independent-reset
dataset requested by the regression procedure: it does not power-cycle UART
peripherals. Seven of eleven such resets completed all seven one-frame smoke
profiles; the other four completed through 1024 bytes and then stopped in the
UART0 traffic generator before producing the 2048-byte row. They reported no
RX drop or adapter error before that point. Independent power-cycle statistics
remain a separate hardware-fixture task; the 7,700-frame uninterrupted run is
the sustained RX adapter result for this round.

USB CDC data ingress remained intentionally out of scope, but its benchmark
configuration was retained and built successfully. `BENCH_PAYLOAD_START_INDEX`
and `BENCH_PAYLOAD_END_INDEX` can select a bounded subset for future external
fixture runs.

## ESP32-S3 full-duplex adapter validation — 2026-08-30

The unified Zephyr adapter was then validated on the same board and
GPIO16-to-GPIO18 fixture. UART1 async RX DMA remained enabled while Wirelink
submitted one unreliable frame through UART1 async TX. The adapter received
`UART_TX_DONE`, waited for `uart_irq_tx_complete()` to report physical line
idle, delivered `WL_EVT_TX_SUCCESS` from consumer context, and printed
`wirelink_uart_dma_tx_v1,pass`. The UART0-to-UART1 RX benchmark then continued
without reinitializing the adapter. UART1 TX was not routed to an external
receiver in this fixture, so this validates driver/adapter ownership and
completion timing rather than the TX waveform or payload at another device.

The physical-idle check matters on this driver: its DMA completion can precede
the last byte leaving the UART FIFO. Wirelink exposes the check as the opt-in
`wait_for_tx_idle` adapter setting so platforms whose async event already has
the standard final-line semantics do not depend on the interrupt-driven query.

The final no-debug image completed the first three 10,000-frame profiles with
zero dropped bytes and no adapter error:

| Payload | Frames | Accepted bytes | Producer cycles | Median cycles | Median | p95 cycles | p99 cycles | Max cycles |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 10,000 | 440,000 | 1,490,000 | 60,507 | 252.1 us | 60,637 | 60,712 | 64,969 |
| 20 | 10,000 | 480,000 | 1,490,000 | 66,014 | 275.1 us | 66,419 | 66,434 | 68,939 |
| 64 | 10,000 | 920,000 | 1,490,000 | 100,769 | 419.9 us | 101,021 | 101,067 | 105,691 |

This run is a lifecycle and concurrency regression gate rather than a new RX
performance baseline: its purpose is to prove that TX completion, physical
idle deferral, and subsequent RX DMA release/re-arm coexist on the same
adapter instance.

## ESP32-S3 continuous/burst RX failure and fix — 2026-09-22

The stop-and-wait rounds above keep the line idle between frames and never
reach the buffer-full path. A continuous-stream workload added to
`benchmarks/zephyr/rx_backend` (`WIRELINK_BENCH_STREAM=ON`) reproduced frame
loss on the former finite-timeout UART DMA ingress. This section records the
original trigger, mechanism, evidence, and the staged-buffer fix.

### Original trigger

On the Zephyr async-UART DMA RX adapter in finite-timeout mode, a peer that
sends more bytes than one direct claim (default 4096) without an idle gap of at
least `timeout_us` (default 200 us) loses frames. Concretely:

- a burst larger than `maximum_chunk` whose gaps stay below the idle timeout;
- a sustained stream whose frame gaps stay below the idle timeout;
- any peer that pipelines frames, retransmits in a burst, or bulk-transfers.

The threshold scales with `maximum_chunk`: a smaller claim fails on a smaller
burst. The failure is not specific to a 100%-duty line. A nominal 1 kHz
six-joint profile (about 130 wire bytes, with roughly 600 us of idle between
1 ms frames) stays below the threshold and is unaffected.

### Original mechanism

The finite-timeout adapter holds exactly one direct claim at a time and
deliberately does not supply a successor: `UART_RX_BUF_REQUEST` pauses when
`timeout_us != SYS_FOREVER_US`.

1. While bytes arrive faster than the idle timeout, the UART DMA fills the
   claim and raises a buffer-full completion instead of an idle completion.
2. The driver republishes the partial prefix (`UART_RX_RDY`), releases the
   buffer (`UART_RX_BUF_RELEASED`) and disables RX because no successor was
   queued (`UART_RX_DISABLED`).
3. RX can only be re-armed after the consumer releases the RX event and
   `wl_zephyr_uart_dma_service()` finishes the slot and calls `start_rx()`.
   During that window the peer keeps sending and nothing drains the UART FIFO,
   so the handoff drops bytes.
4. Re-arming calls `wl_rx_ring_dma_claim()`. Its empty-ring normalization to
   the full physical capacity only applies when the ring is empty; when the
   consumer still holds unread data the claim is limited to the free contiguous
   region. With the default `maximum_chunk` equal to the ring, a full-size claim
   is only possible once the consumer has fully drained, so a lagging consumer
   also forces short claims or a `WOULD_BLOCK` stall.
5. The dropped bytes break the COBS byte stream. The link's overflow recovery
   then discards all currently readable ring bytes to resynchronize
   (`wl_process_rx_stream`), and every following frame is misaligned until the
   next delimiter and fails its payload or CRC check. A small drop therefore
   cascades into many lost frames, which is why `malformed` grows faster than
   `overflow` at large payloads.

Relevant code in the failing design: `adapters/zephyr/uart_dma/src/uart_dma.c`
(`UART_RX_BUF_REQUEST`, `UART_RX_BUF_RELEASED`, `UART_RX_DISABLED`, `service`,
`supply_buffer`),
`src/rx_ring_bipbuf.c` (`wl_rx_ring_dma_claim`, `wl_rx_ring_dma_publish`,
`wl_rx_ring_dma_finish`), and `src/protocol.c` (`wl_process_rx_stream` overflow
recovery).

### Evidence

Fixture: UART0 TX GPIO16 to UART1 RX GPIO18, 3 Mbaud 8-N-1; ring 4096, maximum
claim 4096, idle timeout 200 us; 200 warm-up plus 2000 measured frames per
profile. `received` counts payload-verified frames; `overflow` and `malformed`
are the link RX counters.

| payload | IRQ received | IRQ overflow / malformed | DMA received | DMA overflow | DMA malformed |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 2200 | 0 / 0 | 2162 | 28 | 1 |
| 20 | 2200 | 0 / 0 | 2162 | 57 | 2 |
| 64 | 2200 | 0 / 0 | 2172 | 85 | 2 |
| 120 | 2200 | 0 / 0 | 2149 | 134 | 5 |
| 256 | 2200 | 0 / 0 | 2057 | 287 | 18 |
| 1024 | 2200 | 0 / 0 | 409 | 503 | 1593 |
| 2048 | 2200 | 0 / 0 | 789 | 1601 | 1906 |

The UART IRQ ingress completes every profile at line rate (about 300 kB/s
here). The DMA ingress loses frames from the first full claim onward and
degrades with payload size. `WIRELINK_BENCH_DMA_CHUNK=2048` with a 4096-byte
ring, and `WIRELINK_BENCH_RING=8192` with a 4096-byte claim, both still fail, so
changing the two sizes is not a fix.

For a failing profile the last verified frame can fall inside the warm-up
window, leaving the latency percentiles at zero. A `received` count below the
frame count is the failure signal; zero latency in that case is an artifact.

### Scope

Affected: the Zephyr async-UART DMA RX adapter in finite-timeout mode on the
ESP32-S3, whenever the peer exceeds one claim without an idle gap.

Not affected: UART interrupt ingress; the one-frame full-duplex TX validation;
USB CDC ingress; any link whose peer leaves an idle gap larger than the
configured timeout between frames.

### Reproduction

```sh
west build -b esp32s3_devkitc/esp32s3/procpu \
  /path/to/wirelink/benchmarks/zephyr/rx_backend -d build/rx-stream -- \
  -DWIRELINK_BENCH_INGRESS=DMA -DWIRELINK_BENCH_STREAM=ON
west flash -d build/rx-stream --esp-device /dev/ttyACM0
python benchmarks/zephyr/capture_serial.py --port /dev/ttyACM0 \
  --reset --out stream.log --until "wirelink_rx_bench_v1,error" --seconds 180
```

Use `-DWIRELINK_BENCH_INGRESS=IRQ` for the passing control, and
`-DWIRELINK_BENCH_STREAM_FRAMES=50 -DWIRELINK_BENCH_PAYLOAD_START=<i>
-DWIRELINK_BENCH_PAYLOAD_END=<j>` for a short run.

### Constraints on the fix

- Do not drop a byte between a released claim and the next claim, including
  while the peer keeps sending.
- Keep RX consuming while the consumer lags; do not require an empty ring to
  re-arm.
- Present the consumer a gapless byte stream, so a frame that spans two claims
  decodes as if it were contiguous.
- Preserve the finite-timeout contract and the stop-and-wait latency numbers; a
  peer that leaves an idle gap must keep its current behavior and cost.
- Keep the fix verifiable with the stream workload above: `received == frames`
  with zero `overflow` and `malformed` is the pass condition.

### Resolution and acceptance

The Zephyr adapter now owns two disjoint caller-provided staging buffers and
always queues the second buffer when the driver requests it. `UART_RX_RDY`
prefixes are copied into the core ring in receive order. Released staging bytes
remain retained during temporary ring backpressure, and `service()` answers the
coalesced outstanding buffer request as soon as space is available. The core no
longer needs ordered multi-claim state and permits only one direct-DMA claim.

The stream generator also bounds each TX-owner pass to 256 bytes. It still
keeps the UART FIFO busy, but a short frame can no longer monopolize the main
loop merely because encoding time lets the FIFO accept another complete frame;
RX polling and adapter service therefore run concurrently as the workload
description requires.

Acceptance fixture: ESP32-S3 revision 0.2, Zephyr
`v4.4.0-16126-g933fe1291d22`, 240 MHz, UART0 GPIO16 to UART1 GPIO18, 3 Mbaud,
4096-byte ring, two 256-byte staging buffers, 200 us timeout, and 200 warm-up
plus 2000 measured frames per profile.

| payload | frames | received | dropped | overflow | malformed | bad integrity | adapter errors |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 2200 | 2200 | 0 | 0 | 0 | 0 | 0 |
| 20 | 2200 | 2200 | 0 | 0 | 0 | 0 | 0 |
| 64 | 2200 | 2200 | 0 | 0 | 0 | 0 | 0 |
| 120 | 2200 | 2200 | 0 | 0 | 0 | 0 | 0 |
| 256 | 2200 | 2200 | 0 | 0 | 0 | 0 | 0 |
| 1024 | 2200 | 2200 | 0 | 0 | 0 | 0 | 0 |
| 2048 | 2200 | 2200 | 0 | 0 | 0 | 0 | 0 |

The final benchmark status was `wirelink_rx_bench_v1,error,...,0`; the startup
full-duplex UART DMA TX validation also passed. A separate stop-and-wait run
with 100 warm-up and 1000 measured frames per profile completed all seven
profiles with zero dropped bytes, zero adapter errors, and final status zero,
covering finite-timeout short releases as well as continuous buffer turnover.

## Regression procedure

Correctness is a gate: no corrupted delivery, invalid lease, unexpected drop,
or failed SPSC test is permitted. For a future hardware comparison, use five
independent reset runs per image, preserve every raw CSV log, and compare the
same firmware revision, payload set (16, 20, 64, 120, 256, 1024, 2048),
frequency, buffer sizes, and UART fixture. The 20-byte and 120-byte profiles
represent one joint command and a six-joint aggregate command respectively.
Compare UART producer cycles per byte first; if a future change is within
measurement noise, then compare throughput, p95/p99 latency, and image/RAM
size.

The ESP32-S3 benchmark remains build-only in Twister and can be compiled with:

```sh
cd /path/to/zephyr
../.venv/bin/west twister -T /path/to/wirelink/benchmarks/zephyr/rx_backend \
  -p esp32s3_devkitc/esp32s3/procpu --build-only
```

Physical UART measurement requires the GPIO16-to-GPIO18 independent-UART
fixture and is never performed by Twister.
